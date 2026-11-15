#include "board.h"
#include "library.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"
#include "esp_lvgl_port.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "sdmmc_cmd.h"

#define ENC_A 10
#define ENC_B 11
#define ENC_SW 12

static i2c_master_dev_handle_t touch;
static sdmmc_card_t *card;
static int previous;
static int encoder_rest;
static int edges;
static int encoder_steps;
static portMUX_TYPE encoder_lock = portMUX_INITIALIZER_UNLOCKED;

static void encoder_edge(void *arg)
{
    (void)arg;
    static const int8_t transition[] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
    portENTER_CRITICAL_ISR(&encoder_lock);
    int current = (gpio_get_level(ENC_A)<<1) | gpio_get_level(ENC_B);
    if ((previous ^ current) == 3) edges = 0;
    edges += transition[(previous << 2) | current];
    previous = current;
    if (current == encoder_rest) {
        if (edges == 4 || edges == -4) encoder_steps += edges / 4;
        edges = 0;
    }
    portEXIT_CRITICAL_ISR(&encoder_lock);
}

static void touch_read(lv_indev_t *device, lv_indev_data_t *data)
{
    (void)device;
    uint8_t reg = 2, sample[5];
    data->state = LV_INDEV_STATE_RELEASED;
    if (i2c_master_transmit_receive(touch, &reg, 1, sample, sizeof(sample), 8) != ESP_OK ||
        !(sample[0] & 0xf) || (sample[1] >> 6) == 1) return;
    int x = ((sample[1] & 15) << 8) | sample[2];
    int y = ((sample[3] & 15) << 8) | sample[4];
    if (x >= 320 || y >= 480) return;
    // Transforma las coordenadas verticales del sensor al origen
    // superior izquierdo del LCD en horizontal.
    data->point.x = y;
    data->point.y = 319 - x;
    data->state = LV_INDEV_STATE_PRESSED;
}

bool board_display(void)
{
    gpio_set_direction(45, GPIO_MODE_OUTPUT);
    gpio_set_level(45, 0);
    esp_lcd_i80_bus_handle_t bus;
    esp_lcd_i80_bus_config_t bus_cfg = {
        .dc_gpio_num = 0, .wr_gpio_num = 47, .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums = {9,46,3,8,18,17,16,15}, .bus_width = 8,
        .max_transfer_bytes = 480 * 24 * 2,
    };
    if (esp_lcd_new_i80_bus(&bus_cfg, &bus) != ESP_OK) return false;
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_i80_config_t io_cfg = {
        .cs_gpio_num = -1, .pclk_hz = 20000000, .trans_queue_depth = 2,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8,
        .dc_levels = {.dc_idle_level = 0, .dc_cmd_level = 0, .dc_dummy_level = 0, .dc_data_level = 1},
    };
    if (esp_lcd_new_panel_io_i80(bus, &io_cfg, &io) != ESP_OK) return false;
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = 4, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR, .bits_per_pixel = 16,
    };
    if (esp_lcd_new_panel_st7796(io, &panel_cfg, &panel) != ESP_OK ||
        esp_lcd_panel_reset(panel) != ESP_OK || esp_lcd_panel_init(panel) != ESP_OK ||
        esp_lcd_panel_invert_color(panel, true) != ESP_OK ||
        esp_lcd_panel_disp_on_off(panel, true) != ESP_OK) return false;

    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_max_sleep_ms = 20;
    if (lvgl_port_init(&port) != ESP_OK) return false;
    lvgl_port_display_cfg_t display = {
        .io_handle = io, .panel_handle = panel, .buffer_size = 480 * 24,
        .double_buffer = true, .hres = 480, .vres = 320,
        .rotation = {.swap_xy = true},
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {.buff_dma = true, .swap_bytes = true},
    };
    lv_display_t *screen = lvgl_port_add_disp(&display);
    if (!screen) return false;

    i2c_master_bus_handle_t i2c;
    i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_NUM_0, .sda_io_num = 6, .scl_io_num = 5,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x38, .scl_speed_hz = 400000,
    };
    if (i2c_new_master_bus(&i2c_cfg, &i2c) == ESP_OK &&
        i2c_master_bus_add_device(i2c, &dev, &touch) == ESP_OK && lvgl_port_lock(0)) {
        lv_indev_t *input = lv_indev_create();
        lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(input, screen);
        lv_indev_set_read_cb(input, touch_read);
        lv_timer_set_period(lv_indev_get_read_timer(input), 12);
        lvgl_port_unlock();
    }

    gpio_config_t encoder = {
        .pin_bit_mask = (1ULL<<ENC_A)|(1ULL<<ENC_B)|(1ULL<<ENC_SW),
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&encoder);
    previous = encoder_rest = (gpio_get_level(ENC_A)<<1) | gpio_get_level(ENC_B);
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(ENC_A,encoder_edge,NULL));
    ESP_ERROR_CHECK(gpio_isr_handler_add(ENC_B,encoder_edge,NULL));
    ESP_ERROR_CHECK(gpio_set_intr_type(ENC_A,GPIO_INTR_ANYEDGE));
    ESP_ERROR_CHECK(gpio_set_intr_type(ENC_B,GPIO_INTR_ANYEDGE));
    gpio_set_level(45, 1);
    return true;
}

bool board_sd(void)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus = {
        .mosi_io_num = 40, .miso_io_num = 38, .sclk_io_num = 39,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 4096,
    };
    if (spi_bus_initialize(host.slot, &bus, SDSPI_DEFAULT_DMA) != ESP_OK) return false;
    sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev.gpio_cs = 41;
    dev.host_id = host.slot;
    esp_vfs_fat_sdmmc_mount_config_t cfg = {.max_files = 12};
    return esp_vfs_fat_sdspi_mount(SD_ROOT, &host, &dev, &cfg, &card) == ESP_OK;
}

int board_encoder(void)
{
    portENTER_CRITICAL(&encoder_lock);
    int step = (encoder_steps > 0) - (encoder_steps < 0);
    encoder_steps -= step;
    portEXIT_CRITICAL(&encoder_lock);
    return step;
}

bool board_pressed(void) { return !gpio_get_level(ENC_SW); }

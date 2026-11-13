#include "board.h"
#include "config.h"
#include "esp_log.h"
#include "library.h"
#include "network.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <time.h>

void gallery_start(void);

static void load_card(void *arg)
{
    (void)arg;
    if (board_sd() && !library_init()) ESP_LOGE("sd", "No se pudo cargar la biblioteca");
    vTaskDelete(NULL);
}

void app_main(void)
{
    setenv("TZ", TIMEZONE, 1);
    tzset();
    esp_err_t nvs = nvs_flash_init();
    if (board_display()) gallery_start();
    else ESP_LOGE("lcd", "No se pudo iniciar la pantalla");
    xTaskCreate(load_card,"sd-index",6144,NULL,2,NULL);
    if (nvs == ESP_OK) network_start();
    else ESP_LOGE("nvs", "No se pudo abrir NVS: %s", esp_err_to_name(nvs));
}

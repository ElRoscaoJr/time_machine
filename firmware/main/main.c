#include "board.h"
#include "config.h"
#include "esp_log.h"
#include "library.h"
#include "locality.h"
#include "network.h"
#include "welcome.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <time.h>

void gallery_start(bool intro);

static void load_card(void *arg)
{
    (void)arg;
    bool mounted = board_sd();
    if (mounted) locality_init();
    if (!library_init(mounted) && mounted) ESP_LOGE("sd", "No se pudo cargar la biblioteca");
    vTaskDelete(NULL);
}

void app_main(void)
{
    setenv("TZ", TIMEZONE, 1);
    tzset();
    esp_err_t nvs = nvs_flash_init();
    bool intro = false;
    if (nvs == ESP_OK) {
        esp_err_t error = welcome_pending(&intro);
        if (error != ESP_OK)
            ESP_LOGE("bienvenida", "No se pudo leer la activación: %s", esp_err_to_name(error));
    }
    if (board_display()) gallery_start(intro);
    else ESP_LOGE("lcd", "No se pudo iniciar la pantalla");
    xTaskCreate(load_card,"sd-index",6144,NULL,2,NULL);
    if (nvs == ESP_OK) network_start();
    else ESP_LOGE("nvs", "No se pudo abrir NVS: %s", esp_err_to_name(nvs));
}

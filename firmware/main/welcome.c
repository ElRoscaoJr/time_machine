#include "welcome.h"
#include "nvs.h"

static const char key[] = "next_boot";

esp_err_t welcome_pending(bool *pending)
{
    *pending = false;
    nvs_handle_t handle;
    esp_err_t error = nvs_open("bienvenida", NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error != ESP_OK) return error;
    uint8_t value = 0;
    error = nvs_get_u8(handle, key, &value);
    nvs_close(handle);
    *pending = error == ESP_OK && value == 1;
    return error == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : error;
}

esp_err_t welcome_schedule(bool pending)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open("bienvenida", NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    error = nvs_set_u8(handle, key, pending ? 1 : 0);
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}


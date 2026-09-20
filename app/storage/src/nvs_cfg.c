#include "nvs_cfg.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "nvs_cfg";
static const char *NAMESPACE = "faceattend";

esp_err_t nvs_cfg_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs erase failed: %s", esp_err_to_name(err));
            return err;
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs init failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t nvs_cfg_get_u32(const char *key, uint32_t default_value, uint32_t *value_out)
{
    if (key == NULL || value_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *value_out = default_value;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    uint32_t value = default_value;
    err = nvs_get_u32(handle, key, &value);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *value_out = default_value;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    *value_out = value;
    return ESP_OK;
}

esp_err_t nvs_cfg_set_u32(const char *key, uint32_t value)
{
    if (key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u32(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

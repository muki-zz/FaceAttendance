#include "app_ctx.h"
#include "app_gui.h"
#include "cloud_service.h"
#include "face_service.h"
#include "storage.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";
static app_ctx_t s_app_ctx;

void app_main(void)
{
    esp_err_t err = storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "storage init failed: %s", esp_err_to_name(err));
        return;
    }

    err = app_ctx_create(&s_app_ctx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(err));
        return;
    }

    err = cloud_service_start();
    if (err != ESP_OK) {
        // Cloud/time service is optional for the core offline attendance path.
        ESP_LOGW(TAG, "cloud service start failed, continue offline: %s", esp_err_to_name(err));
    }

    err = face_service_start(&s_app_ctx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "face service start failed: %s", esp_err_to_name(err));
        return;
    }

    if (xTaskCreatePinnedToCore(gui_task_entry,
                                "lvgl_gui",
                                12288,
                                &s_app_ctx,
                                4,
                                NULL,
                                0) != pdPASS) {
        ESP_LOGE(TAG, "create gui task failed");
        return;
    }

    ESP_LOGI(TAG, "FaceAttend face/gui/storage/cloud services started");
}

#include "face_service.h"

#include <string.h>

#include "camera_common.h"
#include "face_common.h"
#include "frame.h"
#include "storage.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

static const char *TAG = "face_service";

static QueueHandle_t s_infer_queue = NULL;
static QueueHandle_t s_preview_queue = NULL;
static QueueHandle_t s_command_queue = NULL;
static QueueHandle_t s_event_queue = NULL;
static volatile bool s_preview_enabled = true;
static bool s_started = false;

static camera_capture_args_t s_capture_args;
static camera_preview_args_t s_preview_args;

#define FACE_ENGINE_CLOUD_START_DELAY_MS 7000
#define FACE_ENGINE_LAUNCHER_STACK_SIZE   3072

static void face_engine_launcher_task(void *arg)
{
    uint32_t delay_ms = (uint32_t)(uintptr_t)arg;

    if (delay_ms > 0) {
        ESP_LOGI(TAG,
                 "delay face engine %lu ms so camera/Wi-Fi can reserve internal RAM first",
                 (unsigned long)delay_ms);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    esp_err_t err =
        face_engine_start(s_infer_queue, s_command_queue, s_event_queue);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "face engine start failed: %s", esp_err_to_name(err));

        face_event_t event = {0};
        event.type = FACE_EVENT_ERROR;
        event.err = err;
        if (s_event_queue != NULL) {
            (void)xQueueSend(s_event_queue, &event, 0);
        }
    }

    vTaskDelete(NULL);
}

QueueHandle_t face_service_get_event_queue(void)
{
    return s_event_queue;
}

void face_service_set_preview_enabled(bool enabled)
{
    s_preview_enabled = enabled;
}

bool face_service_is_preview_enabled(void)
{
    return s_preview_enabled;
}

esp_err_t face_service_request_enroll(const face_enroll_request_t *request)
{
    if (!s_started || s_command_queue == NULL || request == NULL ||
        request->student_id[0] == '\0' || request->name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    face_command_t command = {0};
    command.type = FACE_CMD_ENROLL;
    command.request_us = esp_timer_get_time();
    command.enroll = *request;

    if (xQueueSend(s_command_queue, &command, 0) != pdPASS) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t face_service_start(app_ctx_t *ctx)
{
    if (ctx == NULL || ctx->lcd == NULL ||
        ctx->i2c_bus == NULL || ctx->io_expander == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    s_infer_queue = xQueueCreate(1, sizeof(camera_frame_t));
    s_preview_queue = xQueueCreate(1, sizeof(camera_frame_t));
    s_command_queue = xQueueCreate(4, sizeof(face_command_t));
    s_event_queue = xQueueCreate(16, sizeof(face_event_t));

    if (s_infer_queue == NULL ||
        s_preview_queue == NULL ||
        s_command_queue == NULL ||
        s_event_queue == NULL) {
        ESP_LOGE(TAG, "create queue failed");
        return ESP_ERR_NO_MEM;
    }

    /*
     * IMPORTANT STARTUP ORDER
     * -----------------------
     * Start camera capture/preview first, but do not allocate the ESP-DL face
     * detector/recognizer yet.
     *
     * cloud_service starts before face_service and currently initializes Wi-Fi
     * after about 5 seconds. The face model used to start immediately and
     * consume/fragment internal RAM before esp_wifi_init(), which caused:
     *
     *   wifi:malloc buffer fail
     *   Expected to init 10 rx buffer, actual is 8
     *   ESP_ERR_NO_MEM
     *
     * Keeping the camera alive while delaying only the inference model preserves
     * camera startup stability and lets Wi-Fi reserve its mandatory DMA/internal
     * buffers first.
     */
    s_capture_args.ctx = ctx;
    s_capture_args.infer_queue = s_infer_queue;
    s_capture_args.preview_queue = s_preview_queue;

    s_preview_args.ctx = ctx;
    s_preview_args.preview_queue = s_preview_queue;

    if (xTaskCreatePinnedToCore(camera_preview_task_entry,
                                "cam_preview",
                                5120,
                                &s_preview_args,
                                3,
                                NULL,
                                0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreatePinnedToCore(camera_capture_task,
                                "cam_capture",
                                6144,
                                &s_capture_args,
                                5,
                                NULL,
                                0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    attendance_config_t cfg = {0};
    uint32_t engine_delay_ms = 0;

    if (storage_attendance_config_load(&cfg) == ESP_OK &&
        cfg.cloud_enabled) {
        /*
         * cloud_service.c currently uses a 5 s network-start delay.
         * 7 s gives esp_wifi_init()/esp_wifi_start() time to finish before
         * the model task and its 14 KB internal stack are allocated.
         */
        engine_delay_ms = FACE_ENGINE_CLOUD_START_DELAY_MS;
    }

    if (engine_delay_ms == 0) {
        esp_err_t err =
            face_engine_start(s_infer_queue, s_command_queue, s_event_queue);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "face engine start failed: %s", esp_err_to_name(err));
            return err;
        }
    } else {
        if (xTaskCreate(face_engine_launcher_task,
                        "face_launcher",
                        FACE_ENGINE_LAUNCHER_STACK_SIZE,
                        (void *)(uintptr_t)engine_delay_ms,
                        2,
                        NULL) != pdPASS) {
            ESP_LOGE(TAG, "create face launcher task failed");
            return ESP_ERR_NO_MEM;
        }
    }

    s_started = true;
    ESP_LOGI(TAG,
             "face service started (engine_delay=%lu ms)",
             (unsigned long)engine_delay_ms);
    return ESP_OK;
}

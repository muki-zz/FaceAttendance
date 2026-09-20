#include "camera_common.h"

#include <string.h>

#include "camera_device.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "face_service.h"
#include "frame.h"
#include "freertos/task.h"

static const char *TAG = "cam_capture";

void camera_frame_release(camera_frame_t *frame)
{
    if (frame == NULL) {
        return;
    }
    if (frame->buf != NULL) {
        heap_caps_free(frame->buf);
    }
    *frame = (camera_frame_t){0};
}

static bool queue_send_latest(QueueHandle_t queue, camera_frame_t *frame)
{
    if (queue == NULL || frame == NULL || frame->buf == NULL) {
        return false;
    }

    if (xQueueSend(queue, frame, 0) == pdPASS) {
        *frame = (camera_frame_t){0};
        return true;
    }

    camera_frame_t stale = {0};
    if (xQueueReceive(queue, &stale, 0) == pdPASS) {
        camera_frame_release(&stale);
    }

    if (xQueueSend(queue, frame, 0) == pdPASS) {
        *frame = (camera_frame_t){0};
        return true;
    }
    return false;
}

static bool clone_full_frame(const camera_fb_t *fb, camera_frame_t *out)
{
    if (fb == NULL || out == NULL || fb->buf == NULL || fb->len == 0) {
        return false;
    }

    *out = (camera_frame_t){0};
    out->buf = heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (out->buf == NULL) {
        return false;
    }
    memcpy(out->buf, fb->buf, fb->len);
    out->w = fb->width;
    out->h = fb->height;
    out->len = fb->len;
    out->captured_us = esp_timer_get_time();
    return true;
}

static bool make_preview_frame(const camera_fb_t *fb, camera_frame_t *out)
{
    if (fb == NULL || out == NULL || fb->buf == NULL ||
        fb->width == 0 || fb->height == 0 || fb->format != PIXFORMAT_RGB565) {
        return false;
    }

    const uint16_t dst_w = FACE_PREVIEW_W;
    const uint16_t dst_h = FACE_PREVIEW_H;
    const size_t dst_len = (size_t)dst_w * dst_h * 2U;

    *out = (camera_frame_t){0};
    out->buf = heap_caps_malloc(dst_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (out->buf == NULL) {
        return false;
    }

    for (uint16_t y = 0; y < dst_h; ++y) {
        uint32_t sy = ((uint32_t)y * fb->height) / dst_h;
        for (uint16_t x = 0; x < dst_w; ++x) {
            uint32_t sx = ((uint32_t)x * fb->width) / dst_w;
            size_t src_off = ((size_t)sy * fb->width + sx) * 2U;
            size_t dst_off = ((size_t)y * dst_w + x) * 2U;
            out->buf[dst_off] = fb->buf[src_off];
            out->buf[dst_off + 1] = fb->buf[src_off + 1];
        }
    }

    out->w = dst_w;
    out->h = dst_h;
    out->len = dst_len;
    out->captured_us = esp_timer_get_time();
    return true;
}

void camera_capture_task(void *arg)
{
    camera_capture_args_t *args = (camera_capture_args_t *)arg;
    if (args == NULL || args->ctx == NULL || args->infer_queue == NULL || args->preview_queue == NULL) {
        ESP_LOGE(TAG, "invalid args");
        vTaskDelete(NULL);
        return;
    }

    app_ctx_t *ctx = args->ctx;
    camera_device_config_t camera_config;
    camera_device_default_config(&camera_config);
    camera_config.pixel_format = PIXFORMAT_RGB565;
    camera_config.frame_size = FRAMESIZE_QVGA;

    esp_err_t err = camera_device_open(&ctx->camera, ctx->i2c_bus, ctx->io_expander, &camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "camera open failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "camera capture started");

    while (1) {
        camera_fb_t *fb = NULL;
        err = camera_device_get_frame(&ctx->camera, &fb);
        if (err != ESP_OK || fb == NULL) {
            ESP_LOGW(TAG, "get frame failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (fb->format != PIXFORMAT_RGB565 || fb->len < (size_t)fb->width * fb->height * 2U) {
            ESP_LOGW(TAG, "unexpected camera frame");
            camera_device_return_frame(&ctx->camera, &fb);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        // Do not allocate a full QVGA inference copy while one is already queued.
        // The model task is slower than camera capture on ESP32-S3, so this avoids PSRAM churn.
        if (uxQueueSpacesAvailable(args->infer_queue) > 0) {
            camera_frame_t infer = {0};
            if (clone_full_frame(fb, &infer)) {
                if (xQueueSend(args->infer_queue, &infer, 0) != pdPASS) {
                    camera_frame_release(&infer);
                }
            } else {
                ESP_LOGW(TAG, "alloc infer frame failed");
            }
        }

        if (face_service_is_preview_enabled()) {
            camera_frame_t preview = {0};
            if (make_preview_frame(fb, &preview)) {
                if (!queue_send_latest(args->preview_queue, &preview)) {
                    camera_frame_release(&preview);
                }
            }
        }

        camera_device_return_frame(&ctx->camera, &fb);
        vTaskDelay(pdMS_TO_TICKS(60));
    }
}

#include "camera_common.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "face_service.h"
#include "frame.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lcd_display.h"

#define PREVIEW_ROWS_PER_BATCH 20

static const char *TAG = "cam_preview";

void camera_preview_task_entry(void *arg)
{
    camera_preview_args_t *args = (camera_preview_args_t *)arg;
    if (args == NULL || args->ctx == NULL || args->ctx->lcd == NULL || args->preview_queue == NULL) {
        ESP_LOGE(TAG, "invalid args");
        vTaskDelete(NULL);
        return;
    }

    const size_t line_size = (size_t)FACE_PREVIEW_W * PREVIEW_ROWS_PER_BATCH * 2U;
    uint8_t *line = heap_caps_malloc(line_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (line == NULL) {
        ESP_LOGE(TAG, "alloc DMA line failed");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        camera_frame_t frame = {0};
        if (xQueueReceive(args->preview_queue, &frame, portMAX_DELAY) != pdPASS) {
            continue;
        }

        if (!face_service_is_preview_enabled() || frame.buf == NULL ||
            frame.w != FACE_PREVIEW_W || frame.h != FACE_PREVIEW_H) {
            camera_frame_release(&frame);
            continue;
        }

        if (args->ctx->lcd_mutex != NULL) {
            xSemaphoreTake(args->ctx->lcd_mutex, portMAX_DELAY);
        }

        for (int y = 0; y < FACE_PREVIEW_H; y += PREVIEW_ROWS_PER_BATCH) {
            int rows = PREVIEW_ROWS_PER_BATCH;
            if (y + rows > FACE_PREVIEW_H) {
                rows = FACE_PREVIEW_H - y;
            }
            size_t bytes = (size_t)FACE_PREVIEW_W * rows * 2U;
            memcpy(line, frame.buf + (size_t)y * FACE_PREVIEW_W * 2U, bytes);
            esp_err_t err = lcd_display_draw_bitmap(args->ctx->lcd,
                                                    FACE_PREVIEW_X,
                                                    FACE_PREVIEW_Y + y,
                                                    FACE_PREVIEW_X + FACE_PREVIEW_W,
                                                    FACE_PREVIEW_Y + y + rows,
                                                    line);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "draw preview failed: %s", esp_err_to_name(err));
                break;
            }
        }

        if (args->ctx->lcd_mutex != NULL) {
            xSemaphoreGive(args->ctx->lcd_mutex);
        }
        camera_frame_release(&frame);
    }
}

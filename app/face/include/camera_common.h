#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "app_ctx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    app_ctx_t *ctx;
    QueueHandle_t infer_queue;
    QueueHandle_t preview_queue;
} camera_capture_args_t;

typedef struct {
    app_ctx_t *ctx;
    QueueHandle_t preview_queue;
} camera_preview_args_t;

void camera_capture_task(void *arg);
void camera_preview_task_entry(void *arg);

#ifdef __cplusplus
}
#endif

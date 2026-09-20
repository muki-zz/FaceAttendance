#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "face_service.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FACE_CMD_ENROLL = 1,
} face_command_type_t;

typedef struct {
    face_command_type_t type;
    int64_t request_us;
    face_enroll_request_t enroll;
} face_command_t;

esp_err_t face_engine_start(QueueHandle_t frame_queue,
                            QueueHandle_t command_queue,
                            QueueHandle_t event_queue);

#ifdef __cplusplus
}
#endif

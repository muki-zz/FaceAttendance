#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "app_ctx.h"
#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FACE_PREVIEW_X 10
#define FACE_PREVIEW_Y 52
#define FACE_PREVIEW_W 240
#define FACE_PREVIEW_H 180

typedef enum {
    FACE_ENROLL_FAIL_NONE = 0,
    FACE_ENROLL_FAIL_DUPLICATE_STUDENT,
    FACE_ENROLL_FAIL_DUPLICATE_FACE,
    FACE_ENROLL_FAIL_NO_FACE,
    FACE_ENROLL_FAIL_MULTI_FACE,
    FACE_ENROLL_FAIL_MODEL,
    FACE_ENROLL_FAIL_STORAGE,
    FACE_ENROLL_FAIL_BUSY,
} face_enroll_fail_reason_t;

typedef enum {
    FACE_CHECKIN_RESULT_NONE = 0,
    FACE_CHECKIN_RESULT_SAVED,
    FACE_CHECKIN_RESULT_DUPLICATE,
    FACE_CHECKIN_RESULT_BEFORE_WINDOW,
    FACE_CHECKIN_RESULT_AFTER_WINDOW,
    FACE_CHECKIN_RESULT_TIME_UNSYNCED_SAVED,
    FACE_CHECKIN_RESULT_TIME_SYNCING,
    FACE_CHECKIN_RESULT_TIME_REQUIRED,
    FACE_CHECKIN_RESULT_NO_SCHEDULE,
    FACE_CHECKIN_RESULT_NO_ACTIVE_SESSION,
    FACE_CHECKIN_RESULT_WRONG_CLASS,
    FACE_CHECKIN_RESULT_STORAGE_ERROR,
} face_checkin_result_t;

typedef enum {
    FACE_EVENT_MODEL_READY = 0,
    FACE_EVENT_ENROLL_STARTED,
    FACE_EVENT_ENROLL_SUCCESS,
    FACE_EVENT_ENROLL_FAILED,
    FACE_EVENT_RECOGNIZED,
    FACE_EVENT_UNKNOWN,
    FACE_EVENT_ERROR,
} face_event_type_t;

typedef struct {
    char student_id[STORAGE_STUDENT_ID_LEN];
    char name[STORAGE_STUDENT_NAME_LEN];
    int class_id;
} face_enroll_request_t;

typedef struct {
    face_event_type_t type;
    esp_err_t err;
    face_enroll_fail_reason_t enroll_fail_reason;
    int face_id;
    float similarity;
    bool checkin_saved;
    bool checkin_time_valid;
    checkin_status_t checkin_status;
    face_checkin_result_t checkin_result;
    int64_t checkin_timestamp;
    uint32_t session_id;
    uint16_t course_id;
    uint8_t period;
    char course_name[STORAGE_COURSE_NAME_LEN];
    char student_id[STORAGE_STUDENT_ID_LEN];
    char name[STORAGE_STUDENT_NAME_LEN];
    int class_id;
} face_event_t;

esp_err_t face_service_start(app_ctx_t *ctx);
esp_err_t face_service_request_enroll(const face_enroll_request_t *request);
QueueHandle_t face_service_get_event_queue(void);

void face_service_set_preview_enabled(bool enabled);
bool face_service_is_preview_enabled(void);

#ifdef __cplusplus
}
#endif

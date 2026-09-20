#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_STUDENT_ID_LEN 24
#define STORAGE_STUDENT_NAME_LEN 32
#define STORAGE_COURSE_NAME_LEN 32
#define STORAGE_MAX_STUDENTS 128

#define STORAGE_BASE_PATH        "/spiffs"
#define FACE_DB_PATH             STORAGE_BASE_PATH "/face_db.bin"
#define STUDENT_DB_PATH          STORAGE_BASE_PATH "/students.jsonl"
#define CHECKIN_LOG_PATH         STORAGE_BASE_PATH "/checkin.jsonl"
#define ATTENDANCE_CONFIG_PATH   STORAGE_BASE_PATH "/attendance_config.json"

#define STORAGE_NVS_KEY_NEXT_RECORD "next_rec"
#define STORAGE_NVS_KEY_UPLOAD_MARK "upload_id"

#define STORAGE_TIMEZONE_LEN        48
#define STORAGE_NTP_SERVER_LEN      64
#define STORAGE_MQTT_URI_LEN        96
#define STORAGE_BEMFA_UID_LEN       65
#define STORAGE_BEMFA_TOPIC_LEN     65
#define STORAGE_TIME_VALID_EPOCH    1700000000LL

typedef enum {
    CHECKIN_STATUS_NORMAL = 0,
    CHECKIN_STATUS_LATE = 1,
    CHECKIN_STATUS_ABSENT = 2,
    CHECKIN_STATUS_TIME_UNSYNCED = 3,
} checkin_status_t;

typedef struct {
    uint16_t start_minute;       /* Minutes after 00:00 when check-in opens. */
    uint16_t late_after_minute;  /* Normal before this minute, late at/after it. */
    uint16_t end_minute;         /* Last minute in which check-in is accepted. */
    uint16_t cooldown_sec;       /* Recognition debounce when wall clock is unavailable. */

    char timezone[STORAGE_TIMEZONE_LEN];
    char ntp_server[STORAGE_NTP_SERVER_LEN];

    bool cloud_enabled;
    char mqtt_broker_uri[STORAGE_MQTT_URI_LEN];
    char bemfa_uid[STORAGE_BEMFA_UID_LEN];
    char bemfa_topic[STORAGE_BEMFA_TOPIC_LEN];
    uint8_t mqtt_qos;
    bool mqtt_retain;
    uint8_t sync_batch_size;
} attendance_config_t;

typedef struct {
    int face_id;
    char student_id[STORAGE_STUDENT_ID_LEN];
    char name[STORAGE_STUDENT_NAME_LEN];
    int class_id;
    int64_t created_ts;
} student_profile_t;

typedef struct {
    uint32_t record_id;
    uint32_t session_id;
    int face_id;
    char student_id[STORAGE_STUDENT_ID_LEN];
    char name[STORAGE_STUDENT_NAME_LEN];
    int64_t timestamp;
    checkin_status_t status;
    int class_id;
    uint16_t course_id;
    uint8_t period;
    char course_name[STORAGE_COURSE_NAME_LEN];
} checkin_record_t;

typedef struct {
    uint32_t student_count;
    uint32_t checkin_count;
    uint32_t normal_count;
    uint32_t late_count;
    uint32_t absent_count;
    uint32_t unsynced_count;
} attendance_stats_t;

extern SemaphoreHandle_t g_fs_mutex;

/** Mount SPIFFS, initialize NVS, sync the embedded attendance config, and recover local counters. */
esp_err_t storage_init(void);

/** Compatibility wrapper retained for old call sites. */
esp_err_t storage_spiffs_init(void);

/** Load /spiffs/attendance_config.json. The SPIFFS copy is synchronized from the embedded project JSON at boot. */
esp_err_t storage_attendance_config_load(attendance_config_t *out);

/** Human-readable stable status name used by GUI and MQTT payloads. */
const char *storage_checkin_status_str(checkin_status_t status);

esp_err_t storage_student_add(const student_profile_t *student);
esp_err_t storage_student_find_by_face_id(int face_id, student_profile_t *out);
esp_err_t storage_student_find_by_student_id(const char *student_id, student_profile_t *out);
int storage_student_list(student_profile_t *out, int max_count);

/** Assigns record_id when it is zero and appends one JSONL record. */
esp_err_t storage_checkin_add(checkin_record_t *record);

/** Compatibility wrapper: appends a copy of the record. */
esp_err_t storage_append_checkin(const checkin_record_t *record);

esp_err_t storage_get_last_checkin_for_student(const char *student_id, checkin_record_t *out);

/** Find a student's record for one timetable session. */
esp_err_t storage_get_checkin_for_student_session(const char *student_id,
                                                  uint32_t session_id,
                                                  checkin_record_t *out);

/** Newest-first page containing only records from the current local day. */
int storage_checkin_list_page_today(checkin_record_t *out,
                                    int max_count,
                                    uint32_t offset_from_newest,
                                    uint32_t *total_count_out);

/** Attendance statistics containing only records from the current local day. */
esp_err_t storage_get_stats_today(attendance_stats_t *out);
int storage_checkin_list_recent(checkin_record_t *out, int max_count);

/**
 * Read one page of check-in records in newest-first order.
 *
 * offset_from_newest=0 returns the newest page. An offset of N skips N newest
 * records before filling out[]. total_count_out receives the total number of
 * valid records in the log.
 */
int storage_checkin_list_page(checkin_record_t *out,
                              int max_count,
                              uint32_t offset_from_newest,
                              uint32_t *total_count_out);
esp_err_t storage_get_stats(attendance_stats_t *out);

/** Read records whose record_id is greater than last_uploaded_id. */
int storage_read_unuploaded_records(checkin_record_t *out, int max_count, uint32_t last_uploaded_id);
esp_err_t storage_get_upload_mark(uint32_t *record_id_out);
esp_err_t storage_set_upload_mark(uint32_t record_id);

#ifdef __cplusplus
}
#endif

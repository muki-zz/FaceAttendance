#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Persistent model:
 *   semester_schedule.json   - weekly rules for the whole semester
 *   schedule_overrides.json  - sparse cancel/replace/add exceptions
 *
 * Runtime model:
 *   only today's <=10 sessions are materialized in RAM.
 */
#define TIMETABLE_SEMESTER_PATH        "/spiffs/semester_schedule.json"
#define TIMETABLE_SEMESTER_TMP_PATH    "/spiffs/semester_schedule.tmp"
#define TIMETABLE_OVERRIDES_PATH       "/spiffs/schedule_overrides.json"
#define TIMETABLE_OVERRIDES_TMP_PATH   "/spiffs/schedule_overrides.tmp"

#define TIMETABLE_MAX_SESSIONS         10
#define TIMETABLE_MAX_RULES            48
#define TIMETABLE_MAX_OVERRIDES        16
#define TIMETABLE_DATE_LEN             11
#define TIMETABLE_SEMESTER_NAME_LEN    24
#define TIMETABLE_COURSE_NAME_LEN      32

/*
 * Used only while one MQTT schedule command is being received/parsed.
 * cloud_service prefers PSRAM for this transient buffer.
 */
#define TIMETABLE_MAX_IMPORT_BYTES     8192

typedef struct {
    uint32_t session_id;
    uint16_t course_id;
    int class_id;
    uint8_t period;
    uint16_t open_minute;
    uint16_t start_minute;
    uint16_t late_minute;
    uint16_t end_minute;
    char course_name[TIMETABLE_COURSE_NAME_LEN];
} timetable_entry_t;

typedef struct {
    char date[TIMETABLE_DATE_LEN];
    uint32_t version;          /* Semester schedule version. */
    uint8_t semester_week;     /* 1..32, or 0 when outside semester range. */
    uint8_t count;
    timetable_entry_t entries[TIMETABLE_MAX_SESSIONS];
} timetable_day_t;

typedef struct {
    char semester[TIMETABLE_SEMESTER_NAME_LEN];
    char semester_start[TIMETABLE_DATE_LEN];
    uint32_t version;
    uint32_t override_version;
    uint8_t week_count;
    uint8_t rule_count;
    uint8_t override_count;
} timetable_info_t;

esp_err_t timetable_init(void);

/*
 * Supported MQTT JSON command types:
 *   semester_schedule
 *   schedule_override
 *   schedule_override_reset
 *
 * Version numbers are monotonic. Replayed/stale versions are ignored.
 */
esp_err_t timetable_import_json(const char *json, size_t len);

/* No scheduler task: today's timetable is built lazily when queried. */
esp_err_t timetable_get_day(timetable_day_t *out);

esp_err_t timetable_get_info(timetable_info_t *out);

bool timetable_matches_date(time_t now);

esp_err_t timetable_get_current_session(time_t now,
                                        timetable_entry_t *out);

esp_err_t timetable_format_local_date(time_t now,
                                      char out[TIMETABLE_DATE_LEN]);

#ifdef __cplusplus
}
#endif

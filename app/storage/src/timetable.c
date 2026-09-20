#include "timetable.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "storage.h"

static const char *TAG = "timetable";

typedef struct {
    uint32_t week_mask;               /* bit0=week1 ... bit31=week32 */
    uint8_t weekday;                  /* Monday=1 ... Sunday=7 */
    timetable_entry_t entry;
} timetable_rule_t;

typedef struct {
    char semester[TIMETABLE_SEMESTER_NAME_LEN];
    char start_date[TIMETABLE_DATE_LEN];
    uint32_t version;
    uint8_t week_count;
    uint8_t rule_count;
    timetable_rule_t rules[TIMETABLE_MAX_RULES];
} timetable_semester_t;

typedef enum {
    OVERRIDE_CANCEL = 1,
    OVERRIDE_REPLACE,
    OVERRIDE_ADD,
} override_action_t;

typedef struct {
    char date[TIMETABLE_DATE_LEN];
    uint8_t action;
    uint8_t period;
    timetable_entry_t entry;
} timetable_override_t;

typedef struct {
    uint32_t version;
    uint8_t count;
    timetable_override_t entries[TIMETABLE_MAX_OVERRIDES];
} timetable_override_set_t;

typedef struct {
    uint32_t version;
    bool reset_all;
    bool restore_one;
    timetable_override_t value;
} override_command_t;

static SemaphoreHandle_t s_lock = NULL;
static timetable_semester_t *s_semester = NULL;
static timetable_override_set_t *s_overrides = NULL;
static timetable_day_t s_day;
static bool s_day_valid = false;
static bool s_initialized = false;


static void *timetable_calloc(size_t size)
{
    void *ptr = heap_caps_calloc(
        1,
        size,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (ptr == NULL) {
        ptr = calloc(1, size);
    }

    return ptr;
}

static void copy_string(char *dst, size_t size, const char *src)
{
    if (dst == NULL || size == 0) {
        return;
    }
    snprintf(dst, size, "%s", src != NULL ? src : "");
}

static bool is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) ||
           (year % 400 == 0);
}

static int month_days(int year, int month)
{
    static const uint8_t days[12] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return days[month - 1];
}

static bool parse_date(const char *date,
                       int *year_out,
                       int *month_out,
                       int *day_out)
{
    if (date == NULL ||
        strlen(date) != 10 ||
        date[4] != '-' ||
        date[7] != '-') {
        return false;
    }

    for (int i = 0; i < 10; ++i) {
        if (i == 4 || i == 7) {
            continue;
        }
        if (!isdigit((unsigned char)date[i])) {
            return false;
        }
    }

    int year = 0;
    int month = 0;
    int day = 0;

    if (sscanf(date, "%d-%d-%d", &year, &month, &day) != 3 ||
        year < 2020 || year > 2099 ||
        month < 1 || month > 12 ||
        day < 1 || day > month_days(year, month)) {
        return false;
    }

    if (year_out != NULL) {
        *year_out = year;
    }
    if (month_out != NULL) {
        *month_out = month;
    }
    if (day_out != NULL) {
        *day_out = day;
    }
    return true;
}

/* Days since 1970-01-01, Gregorian calendar. */
static int32_t civil_day_index(int year, int month, int day)
{
    int y = year - (month <= 2 ? 1 : 0);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    int mp = month + (month > 2 ? -3 : 9);
    unsigned doy =
        (unsigned)((153 * mp + 2) / 5 + day - 1);
    unsigned doe =
        yoe * 365U + yoe / 4U - yoe / 100U + doy;

    return (int32_t)(era * 146097 + (int)doe - 719468);
}

static bool date_index(const char *date, int32_t *out)
{
    int year = 0;
    int month = 0;
    int day = 0;

    if (out == NULL ||
        !parse_date(date, &year, &month, &day)) {
        return false;
    }

    *out = civil_day_index(year, month, day);
    return true;
}

static bool parse_hhmm(const char *text, uint16_t *minutes_out)
{
    if (text == NULL || minutes_out == NULL) {
        return false;
    }

    int hour = -1;
    int minute = -1;
    char tail = '\0';

    if (sscanf(text, "%d:%d%c", &hour, &minute, &tail) != 2 ||
        hour < 0 || hour > 23 ||
        minute < 0 || minute > 59) {
        return false;
    }

    *minutes_out = (uint16_t)(hour * 60 + minute);
    return true;
}

static bool get_minute(cJSON *obj,
                       const char *minute_key,
                       const char *time_key,
                       uint16_t *out)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, minute_key);

    if (cJSON_IsNumber(item) &&
        item->valuedouble >= 0 &&
        item->valuedouble <= 1439) {
        *out = (uint16_t)item->valueint;
        return true;
    }

    item = cJSON_GetObjectItemCaseSensitive(obj, time_key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return parse_hhmm(item->valuestring, out);
    }

    return false;
}

static uint32_t hash_update(uint32_t hash,
                            const void *data,
                            size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;

    for (size_t i = 0; i < len; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t make_session_id(const char *date,
                                const timetable_entry_t *entry)
{
    uint32_t hash = 2166136261u;

    hash = hash_update(hash, date, strlen(date));
    hash = hash_update(hash, &entry->period, sizeof(entry->period));
    hash = hash_update(hash, &entry->course_id, sizeof(entry->course_id));
    hash = hash_update(hash, &entry->class_id, sizeof(entry->class_id));
    hash = hash_update(hash, &entry->start_minute, sizeof(entry->start_minute));

    return hash == 0 ? 1u : hash;
}

static int compare_entry(const void *lhs, const void *rhs)
{
    const timetable_entry_t *a = (const timetable_entry_t *)lhs;
    const timetable_entry_t *b = (const timetable_entry_t *)rhs;

    if (a->start_minute != b->start_minute) {
        return (int)a->start_minute - (int)b->start_minute;
    }
    return (int)a->period - (int)b->period;
}

static int compare_rule(const void *lhs, const void *rhs)
{
    const timetable_rule_t *a = (const timetable_rule_t *)lhs;
    const timetable_rule_t *b = (const timetable_rule_t *)rhs;

    if (a->weekday != b->weekday) {
        return (int)a->weekday - (int)b->weekday;
    }
    return compare_entry(&a->entry, &b->entry);
}

static int compare_override(const void *lhs, const void *rhs)
{
    const timetable_override_t *a = (const timetable_override_t *)lhs;
    const timetable_override_t *b = (const timetable_override_t *)rhs;

    int cmp = strcmp(a->date, b->date);
    if (cmp != 0) {
        return cmp;
    }
    return (int)a->period - (int)b->period;
}

static uint32_t all_weeks_mask(uint8_t week_count)
{
    if (week_count >= 32) {
        return 0xFFFFFFFFu;
    }
    return week_count == 0 ? 0u : ((1u << week_count) - 1u);
}

static bool parse_week_mask(cJSON *item,
                            uint8_t week_count,
                            uint32_t *mask_out)
{
    if (mask_out == NULL || week_count == 0 || week_count > 32) {
        return false;
    }

    uint32_t mask = 0;
    cJSON *value = cJSON_GetObjectItemCaseSensitive(item, "week_mask");

    if (cJSON_IsNumber(value)) {
        if (value->valuedouble < 0.0 ||
            value->valuedouble > 4294967295.0) {
            return false;
        }
        mask = (uint32_t)value->valuedouble;
    } else {
        value = cJSON_GetObjectItemCaseSensitive(item, "weeks");

        if (cJSON_IsArray(value)) {
            int count = cJSON_GetArraySize(value);

            for (int i = 0; i < count; ++i) {
                cJSON *week = cJSON_GetArrayItem(value, i);

                if (!cJSON_IsNumber(week) ||
                    week->valueint < 1 ||
                    week->valueint > week_count) {
                    return false;
                }

                mask |= 1u << (week->valueint - 1);
            }
        } else {
            /* Omitted => this course is active every semester week. */
            mask = all_weeks_mask(week_count);
        }
    }

    mask &= all_weeks_mask(week_count);

    if (mask == 0) {
        return false;
    }

    *mask_out = mask;
    return true;
}

static bool parse_entry(cJSON *item,
                        const char *date,
                        timetable_entry_t *out)
{
    if (!cJSON_IsObject(item) || out == NULL) {
        return false;
    }

    timetable_entry_t entry = {0};

    cJSON *value = cJSON_GetObjectItemCaseSensitive(item, "period");
    if (!cJSON_IsNumber(value) ||
        value->valueint < 1 ||
        value->valueint > 15) {
        return false;
    }
    entry.period = (uint8_t)value->valueint;

    value = cJSON_GetObjectItemCaseSensitive(item, "course_id");
    if (!cJSON_IsNumber(value) ||
        value->valueint < 0 ||
        value->valueint > 65535) {
        return false;
    }
    entry.course_id = (uint16_t)value->valueint;

    value = cJSON_GetObjectItemCaseSensitive(item, "class_id");
    if (!cJSON_IsNumber(value)) {
        return false;
    }
    entry.class_id = value->valueint;

    value = cJSON_GetObjectItemCaseSensitive(item, "course_name");
    if (cJSON_IsString(value) && value->valuestring != NULL) {
        copy_string(entry.course_name,
                    sizeof(entry.course_name),
                    value->valuestring);
    } else {
        snprintf(entry.course_name,
                 sizeof(entry.course_name),
                 "Course %u",
                 (unsigned)entry.course_id);
    }

    if (!get_minute(item, "start_min", "start", &entry.start_minute) ||
        !get_minute(item, "end_min", "end", &entry.end_minute)) {
        return false;
    }

    entry.open_minute = entry.start_minute;
    (void)get_minute(item, "open_min", "open", &entry.open_minute);

    entry.late_minute = entry.start_minute;
    if (!get_minute(item, "late_min", "late_after", &entry.late_minute)) {
        uint16_t candidate = (uint16_t)(entry.start_minute + 10U);
        entry.late_minute =
            candidate <= entry.end_minute ? candidate : entry.start_minute;
    }

    if (entry.open_minute > entry.start_minute ||
        entry.start_minute > entry.late_minute ||
        entry.late_minute > entry.end_minute) {
        return false;
    }

    if (date != NULL) {
        entry.session_id = make_session_id(date, &entry);
    }

    *out = entry;
    return true;
}

static esp_err_t parse_semester_root(cJSON *root,
                                     timetable_semester_t *out)
{
    if (!cJSON_IsObject(root) || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *semester =
        cJSON_GetObjectItemCaseSensitive(root, "semester");
    cJSON *start =
        cJSON_GetObjectItemCaseSensitive(root, "semester_start");
    cJSON *version =
        cJSON_GetObjectItemCaseSensitive(root, "version");
    cJSON *week_count =
        cJSON_GetObjectItemCaseSensitive(root, "week_count");
    cJSON *entries =
        cJSON_GetObjectItemCaseSensitive(root, "entries");

    if (!cJSON_IsString(semester) ||
        semester->valuestring == NULL ||
        semester->valuestring[0] == '\0' ||
        !cJSON_IsString(start) ||
        start->valuestring == NULL ||
        !parse_date(start->valuestring, NULL, NULL, NULL) ||
        !cJSON_IsNumber(version) ||
        version->valuedouble <= 0 ||
        !cJSON_IsNumber(week_count) ||
        week_count->valueint < 1 ||
        week_count->valueint > 32 ||
        !cJSON_IsArray(entries)) {
        return ESP_ERR_INVALID_ARG;
    }

    copy_string(out->semester,
                sizeof(out->semester),
                semester->valuestring);
    copy_string(out->start_date,
                sizeof(out->start_date),
                start->valuestring);

    out->version = (uint32_t)version->valuedouble;
    out->week_count = (uint8_t)week_count->valueint;

    int count = cJSON_GetArraySize(entries);
    if (count < 0 || count > TIMETABLE_MAX_RULES) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (int i = 0; i < count; ++i) {
        cJSON *item = cJSON_GetArrayItem(entries, i);
        cJSON *weekday =
            cJSON_GetObjectItemCaseSensitive(item, "weekday");

        if (!cJSON_IsObject(item) ||
            !cJSON_IsNumber(weekday) ||
            weekday->valueint < 1 ||
            weekday->valueint > 7) {
            return ESP_ERR_INVALID_ARG;
        }

        timetable_rule_t *rule = &out->rules[out->rule_count];
        rule->weekday = (uint8_t)weekday->valueint;

        if (!parse_entry(item, NULL, &rule->entry) ||
            !parse_week_mask(item, out->week_count, &rule->week_mask)) {
            ESP_LOGW(TAG, "invalid semester rule index=%d", i);
            return ESP_ERR_INVALID_ARG;
        }

        out->rule_count++;
    }

    qsort(out->rules,
          out->rule_count,
          sizeof(out->rules[0]),
          compare_rule);

    /*
     * Same weekday/period is allowed for odd/even-week switching,
     * but the week masks must not overlap.
     */
    for (uint8_t i = 0; i < out->rule_count; ++i) {
        for (uint8_t j = (uint8_t)(i + 1U);
             j < out->rule_count;
             ++j) {
            const timetable_rule_t *a = &out->rules[i];
            const timetable_rule_t *b = &out->rules[j];

            if (a->weekday != b->weekday) {
                break;
            }

            if (a->entry.period == b->entry.period &&
                (a->week_mask & b->week_mask) != 0) {
                ESP_LOGE(TAG,
                         "conflicting rule weekday=%u period=%u",
                         (unsigned)a->weekday,
                         (unsigned)a->entry.period);
                return ESP_ERR_INVALID_ARG;
            }
        }
    }

    return ESP_OK;
}

static const char *action_name(uint8_t action)
{
    switch ((override_action_t)action) {
        case OVERRIDE_CANCEL: return "cancel";
        case OVERRIDE_REPLACE: return "replace";
        case OVERRIDE_ADD: return "add";
        default: return "unknown";
    }
}

static bool parse_action(const char *text,
                         uint8_t *action_out,
                         bool *restore_out)
{
    if (text == NULL || action_out == NULL || restore_out == NULL) {
        return false;
    }

    *restore_out = false;

    if (strcmp(text, "cancel") == 0) {
        *action_out = OVERRIDE_CANCEL;
        return true;
    }
    if (strcmp(text, "replace") == 0) {
        *action_out = OVERRIDE_REPLACE;
        return true;
    }
    if (strcmp(text, "add") == 0) {
        *action_out = OVERRIDE_ADD;
        return true;
    }
    if (strcmp(text, "restore") == 0) {
        *action_out = 0;
        *restore_out = true;
        return true;
    }

    return false;
}

static esp_err_t parse_override_command(cJSON *root,
                                        override_command_t *out)
{
    if (!cJSON_IsObject(root) || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *version =
        cJSON_GetObjectItemCaseSensitive(root, "version");

    if (!cJSON_IsNumber(version) ||
        version->valuedouble <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    out->version = (uint32_t)version->valuedouble;

    cJSON *type =
        cJSON_GetObjectItemCaseSensitive(root, "type");

    if (cJSON_IsString(type) &&
        type->valuestring != NULL &&
        strcmp(type->valuestring, "schedule_override_reset") == 0) {
        out->reset_all = true;
        return ESP_OK;
    }

    cJSON *date =
        cJSON_GetObjectItemCaseSensitive(root, "date");
    cJSON *action =
        cJSON_GetObjectItemCaseSensitive(root, "action");
    cJSON *period =
        cJSON_GetObjectItemCaseSensitive(root, "period");

    if (!cJSON_IsString(date) ||
        date->valuestring == NULL ||
        !parse_date(date->valuestring, NULL, NULL, NULL) ||
        !cJSON_IsString(action) ||
        action->valuestring == NULL ||
        !cJSON_IsNumber(period) ||
        period->valueint < 1 ||
        period->valueint > 15) {
        return ESP_ERR_INVALID_ARG;
    }

    copy_string(out->value.date,
                sizeof(out->value.date),
                date->valuestring);
    out->value.period = (uint8_t)period->valueint;

    bool restore = false;
    if (!parse_action(action->valuestring,
                      &out->value.action,
                      &restore)) {
        return ESP_ERR_INVALID_ARG;
    }

    out->restore_one = restore;

    if (restore) {
        return ESP_OK;
    }

    if (out->value.action == OVERRIDE_REPLACE ||
        out->value.action == OVERRIDE_ADD) {
        if (!parse_entry(root,
                         out->value.date,
                         &out->value.entry)) {
            return ESP_ERR_INVALID_ARG;
        }
        out->value.entry.period = out->value.period;
    }

    return ESP_OK;
}

static esp_err_t parse_override_set(cJSON *root,
                                    timetable_override_set_t *out)
{
    if (!cJSON_IsObject(root) || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *version =
        cJSON_GetObjectItemCaseSensitive(root, "version");
    cJSON *entries =
        cJSON_GetObjectItemCaseSensitive(root, "entries");

    if (!cJSON_IsNumber(version) ||
        version->valuedouble < 0 ||
        !cJSON_IsArray(entries)) {
        return ESP_ERR_INVALID_ARG;
    }

    out->version = (uint32_t)version->valuedouble;

    int count = cJSON_GetArraySize(entries);
    if (count < 0 || count > TIMETABLE_MAX_OVERRIDES) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (int i = 0; i < count; ++i) {
        cJSON *item = cJSON_GetArrayItem(entries, i);
        cJSON *date =
            cJSON_GetObjectItemCaseSensitive(item, "date");
        cJSON *action =
            cJSON_GetObjectItemCaseSensitive(item, "action");
        cJSON *period =
            cJSON_GetObjectItemCaseSensitive(item, "period");

        if (!cJSON_IsObject(item) ||
            !cJSON_IsString(date) ||
            date->valuestring == NULL ||
            !parse_date(date->valuestring, NULL, NULL, NULL) ||
            !cJSON_IsString(action) ||
            action->valuestring == NULL ||
            !cJSON_IsNumber(period) ||
            period->valueint < 1 ||
            period->valueint > 15) {
            return ESP_ERR_INVALID_ARG;
        }

        timetable_override_t *dst = &out->entries[out->count];
        copy_string(dst->date, sizeof(dst->date), date->valuestring);
        dst->period = (uint8_t)period->valueint;

        bool restore = false;
        if (!parse_action(action->valuestring,
                          &dst->action,
                          &restore) ||
            restore) {
            return ESP_ERR_INVALID_ARG;
        }

        if (dst->action == OVERRIDE_REPLACE ||
            dst->action == OVERRIDE_ADD) {
            if (!parse_entry(item, dst->date, &dst->entry)) {
                return ESP_ERR_INVALID_ARG;
            }
            dst->entry.period = dst->period;
        }

        out->count++;
    }

    qsort(out->entries,
          out->count,
          sizeof(out->entries[0]),
          compare_override);

    return ESP_OK;
}

static esp_err_t write_atomic(const char *path,
                              const char *tmp_path,
                              const char *text)
{
    if (path == NULL || tmp_path == NULL || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ESP_OK;
    size_t len = strlen(text);

    xSemaphoreTake(g_fs_mutex, portMAX_DELAY);

    FILE *fp = fopen(tmp_path, "w");

    if (fp == NULL) {
        err = ESP_FAIL;
    } else {
        if (fwrite(text, 1, len, fp) != len ||
            fputc('\n', fp) == EOF) {
            err = ESP_FAIL;
        }
        fflush(fp);
        fclose(fp);
    }

    if (err == ESP_OK) {
        remove(path);
        if (rename(tmp_path, path) != 0) {
            err = ESP_FAIL;
        }
    } else {
        remove(tmp_path);
    }

    xSemaphoreGive(g_fs_mutex);
    return err;
}

static esp_err_t read_text(const char *path,
                           char **text_out,
                           size_t *len_out)
{
    if (path == NULL || text_out == NULL || len_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *text_out = NULL;
    *len_out = 0;

    xSemaphoreTake(g_fs_mutex, portMAX_DELAY);

    FILE *fp = fopen(path, "r");

    if (fp == NULL) {
        xSemaphoreGive(g_fs_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    long size = 0;

    if (fseek(fp, 0, SEEK_END) != 0 ||
        (size = ftell(fp)) <= 0 ||
        size > TIMETABLE_MAX_IMPORT_BYTES ||
        fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        xSemaphoreGive(g_fs_mutex);
        return ESP_ERR_INVALID_SIZE;
    }

    char *text = (char *)malloc((size_t)size + 1U);

    if (text == NULL) {
        fclose(fp);
        xSemaphoreGive(g_fs_mutex);
        return ESP_ERR_NO_MEM;
    }

    size_t read_size = fread(text, 1, (size_t)size, fp);

    fclose(fp);
    xSemaphoreGive(g_fs_mutex);

    if (read_size != (size_t)size) {
        free(text);
        return ESP_FAIL;
    }

    text[size] = '\0';
    *text_out = text;
    *len_out = (size_t)size;

    return ESP_OK;
}

static cJSON *entry_to_json(const timetable_entry_t *entry)
{
    cJSON *item = cJSON_CreateObject();

    if (item == NULL) {
        return NULL;
    }

    cJSON_AddNumberToObject(item, "period", entry->period);
    cJSON_AddNumberToObject(item, "course_id", entry->course_id);
    cJSON_AddStringToObject(item, "course_name", entry->course_name);
    cJSON_AddNumberToObject(item, "class_id", entry->class_id);
    cJSON_AddNumberToObject(item, "open_min", entry->open_minute);
    cJSON_AddNumberToObject(item, "start_min", entry->start_minute);
    cJSON_AddNumberToObject(item, "late_min", entry->late_minute);
    cJSON_AddNumberToObject(item, "end_min", entry->end_minute);

    return item;
}

static esp_err_t write_semester(const timetable_semester_t *semester)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *entries = cJSON_CreateArray();

    if (root == NULL || entries == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(entries);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "type", "semester_schedule");
    cJSON_AddStringToObject(root, "semester", semester->semester);
    cJSON_AddStringToObject(root, "semester_start", semester->start_date);
    cJSON_AddNumberToObject(root, "week_count", semester->week_count);
    cJSON_AddNumberToObject(root, "version", semester->version);
    cJSON_AddItemToObject(root, "entries", entries);

    for (uint8_t i = 0; i < semester->rule_count; ++i) {
        const timetable_rule_t *rule = &semester->rules[i];
        cJSON *item = entry_to_json(&rule->entry);

        if (item == NULL) {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }

        cJSON_AddNumberToObject(item, "weekday", rule->weekday);
        cJSON_AddNumberToObject(item, "week_mask", (double)rule->week_mask);
        cJSON_AddItemToArray(entries, item);
    }

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (text == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = write_atomic(TIMETABLE_SEMESTER_PATH,
                                 TIMETABLE_SEMESTER_TMP_PATH,
                                 text);
    cJSON_free(text);

    return err;
}

static esp_err_t write_overrides(const timetable_override_set_t *set)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *entries = cJSON_CreateArray();

    if (root == NULL || entries == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(entries);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "type", "schedule_override_set");
    cJSON_AddNumberToObject(root, "version", set->version);
    cJSON_AddItemToObject(root, "entries", entries);

    for (uint8_t i = 0; i < set->count; ++i) {
        const timetable_override_t *ov = &set->entries[i];
        cJSON *item = cJSON_CreateObject();

        if (item == NULL) {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }

        cJSON_AddStringToObject(item, "date", ov->date);
        cJSON_AddStringToObject(item, "action", action_name(ov->action));
        cJSON_AddNumberToObject(item, "period", ov->period);

        if (ov->action == OVERRIDE_REPLACE ||
            ov->action == OVERRIDE_ADD) {
            cJSON_AddNumberToObject(item, "course_id", ov->entry.course_id);
            cJSON_AddStringToObject(item, "course_name", ov->entry.course_name);
            cJSON_AddNumberToObject(item, "class_id", ov->entry.class_id);
            cJSON_AddNumberToObject(item, "open_min", ov->entry.open_minute);
            cJSON_AddNumberToObject(item, "start_min", ov->entry.start_minute);
            cJSON_AddNumberToObject(item, "late_min", ov->entry.late_minute);
            cJSON_AddNumberToObject(item, "end_min", ov->entry.end_minute);
        }

        cJSON_AddItemToArray(entries, item);
    }

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (text == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = write_atomic(TIMETABLE_OVERRIDES_PATH,
                                 TIMETABLE_OVERRIDES_TMP_PATH,
                                 text);
    cJSON_free(text);

    return err;
}

static esp_err_t read_semester(timetable_semester_t *out)
{
    char *text = NULL;
    size_t len = 0;

    esp_err_t err = read_text(TIMETABLE_SEMESTER_PATH, &text, &len);
    if (err != ESP_OK) {
        return err;
    }

    cJSON *root = cJSON_ParseWithLength(text, len);
    free(text);

    if (root == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");

    if (!cJSON_IsString(type) ||
        type->valuestring == NULL ||
        strcmp(type->valuestring, "semester_schedule") != 0) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    err = parse_semester_root(root, out);
    cJSON_Delete(root);

    return err;
}

static esp_err_t read_overrides(timetable_override_set_t *out)
{
    char *text = NULL;
    size_t len = 0;

    esp_err_t err = read_text(TIMETABLE_OVERRIDES_PATH, &text, &len);
    if (err != ESP_OK) {
        return err;
    }

    cJSON *root = cJSON_ParseWithLength(text, len);
    free(text);

    if (root == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");

    if (!cJSON_IsString(type) ||
        type->valuestring == NULL ||
        strcmp(type->valuestring, "schedule_override_set") != 0) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    err = parse_override_set(root, out);
    cJSON_Delete(root);

    return err;
}

static int find_override(const timetable_override_set_t *set,
                         const char *date,
                         uint8_t period)
{
    for (uint8_t i = 0; i < set->count; ++i) {
        if (set->entries[i].period == period &&
            strcmp(set->entries[i].date, date) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void remove_override(timetable_override_set_t *set,
                            uint8_t index)
{
    if (index >= set->count) {
        return;
    }

    for (uint8_t i = index; i + 1U < set->count; ++i) {
        set->entries[i] = set->entries[i + 1U];
    }

    set->count--;
    memset(&set->entries[set->count],
           0,
           sizeof(set->entries[set->count]));
}

static int find_period(const timetable_day_t *day, uint8_t period)
{
    for (uint8_t i = 0; i < day->count; ++i) {
        if (day->entries[i].period == period) {
            return (int)i;
        }
    }
    return -1;
}

static void remove_period(timetable_day_t *day, uint8_t period)
{
    int index = find_period(day, period);

    if (index < 0) {
        return;
    }

    for (uint8_t i = (uint8_t)index; i + 1U < day->count; ++i) {
        day->entries[i] = day->entries[i + 1U];
    }

    day->count--;
    memset(&day->entries[day->count],
           0,
           sizeof(day->entries[day->count]));
}

static void add_entry(timetable_day_t *day,
                      const timetable_entry_t *entry)
{
    if (day->count >= TIMETABLE_MAX_SESSIONS) {
        ESP_LOGW(TAG,
                 "today exceeds %u sessions; period %u ignored",
                 (unsigned)TIMETABLE_MAX_SESSIONS,
                 (unsigned)entry->period);
        return;
    }

    day->entries[day->count++] = *entry;
}

static esp_err_t format_date(time_t now,
                             char out[TIMETABLE_DATE_LEN])
{
    if (out == NULL ||
        (int64_t)now < STORAGE_TIME_VALID_EPOCH) {
        return ESP_ERR_INVALID_STATE;
    }

    struct tm local = {0};
    localtime_r(&now, &local);

    return strftime(out,
                    TIMETABLE_DATE_LEN,
                    "%Y-%m-%d",
                    &local) > 0
               ? ESP_OK
               : ESP_FAIL;
}

static esp_err_t build_today_locked(time_t now)
{
    char date[TIMETABLE_DATE_LEN] = {0};

    esp_err_t err = format_date(now, date);
    if (err != ESP_OK) {
        return err;
    }

    if (s_day_valid && strcmp(s_day.date, date) == 0) {
        return ESP_OK;
    }

    if (s_semester->version == 0 && s_overrides->count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    timetable_day_t day = {0};
    copy_string(day.date, sizeof(day.date), date);
    day.version = s_semester->version;

    int32_t today_idx = 0;
    int32_t start_idx = 0;

    if (s_semester->version > 0 &&
        date_index(date, &today_idx) &&
        date_index(s_semester->start_date, &start_idx)) {
        int32_t delta = today_idx - start_idx;
        int32_t semester_days = (int32_t)s_semester->week_count * 7;

        if (delta >= 0 && delta < semester_days) {
            uint8_t week = (uint8_t)(delta / 7 + 1);
            day.semester_week = week;

            struct tm local = {0};
            localtime_r(&now, &local);

            uint8_t weekday =
                local.tm_wday == 0 ? 7U : (uint8_t)local.tm_wday;
            uint32_t week_bit = 1u << (week - 1U);

            for (uint8_t i = 0; i < s_semester->rule_count; ++i) {
                const timetable_rule_t *rule = &s_semester->rules[i];

                if (rule->weekday != weekday ||
                    (rule->week_mask & week_bit) == 0) {
                    continue;
                }

                timetable_entry_t entry = rule->entry;
                entry.session_id = make_session_id(date, &entry);
                add_entry(&day, &entry);
            }
        }
    }

    /* Date-specific exceptions win over the weekly template. */
    for (uint8_t i = 0; i < s_overrides->count; ++i) {
        const timetable_override_t *ov = &s_overrides->entries[i];

        if (strcmp(ov->date, date) != 0) {
            continue;
        }

        if (ov->action == OVERRIDE_CANCEL) {
            remove_period(&day, ov->period);
            continue;
        }

        if (ov->action == OVERRIDE_REPLACE ||
            ov->action == OVERRIDE_ADD) {
            timetable_entry_t entry = ov->entry;
            entry.period = ov->period;
            entry.session_id = make_session_id(date, &entry);

            /* Explicit date exception always wins for that period. */
            remove_period(&day, ov->period);
            add_entry(&day, &entry);
        }
    }

    qsort(day.entries,
          day.count,
          sizeof(day.entries[0]),
          compare_entry);

    s_day = day;
    s_day_valid = true;

    ESP_LOGI(TAG,
             "today built date=%s week=%u sessions=%u",
             day.date,
             (unsigned)day.semester_week,
             (unsigned)day.count);

    return ESP_OK;
}

esp_err_t timetable_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    if (g_fs_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_lock = xSemaphoreCreateMutex();

    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_semester =
        (timetable_semester_t *)timetable_calloc(
            sizeof(timetable_semester_t));

    s_overrides =
        (timetable_override_set_t *)timetable_calloc(
            sizeof(timetable_override_set_t));

    if (s_semester == NULL ||
        s_overrides == NULL) {
        free(s_semester);
        free(s_overrides);
        s_semester = NULL;
        s_overrides = NULL;
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err =
        read_semester(s_semester);

    if (err == ESP_ERR_NOT_FOUND) {
        memset(s_semester,
               0,
               sizeof(*s_semester));

        ESP_LOGI(TAG,
                 "no semester schedule stored");
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "ignore invalid semester schedule: %s",
                 esp_err_to_name(err));

        memset(s_semester,
               0,
               sizeof(*s_semester));
    }

    err = read_overrides(s_overrides);

    if (err == ESP_ERR_NOT_FOUND) {
        memset(s_overrides,
               0,
               sizeof(*s_overrides));
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "ignore invalid schedule overrides: %s",
                 esp_err_to_name(err));

        memset(s_overrides,
               0,
               sizeof(*s_overrides));
    }

    memset(&s_day, 0, sizeof(s_day));
    s_day_valid = false;
    s_initialized = true;

    ESP_LOGI(
        TAG,
        "ready semester=%s v=%lu rules=%u override_v=%lu overrides=%u",
        s_semester->semester[0] != '\0'
            ? s_semester->semester
            : "(none)",
        (unsigned long)s_semester->version,
        (unsigned)s_semester->rule_count,
        (unsigned long)s_overrides->version,
        (unsigned)s_overrides->count);

    return ESP_OK;
}

static esp_err_t import_semester(cJSON *root)
{
    timetable_semester_t *parsed =
        (timetable_semester_t *)timetable_calloc(sizeof(timetable_semester_t));

    if (parsed == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = parse_semester_root(root, parsed);

    if (err != ESP_OK) {
        free(parsed);
        return err;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        free(parsed);
        return ESP_ERR_TIMEOUT;
    }

    bool same_identity =
        strcmp(s_semester->semester, parsed->semester) == 0 &&
        strcmp(s_semester->start_date, parsed->start_date) == 0;
    bool stale =
        same_identity && s_semester->version >= parsed->version;

    xSemaphoreGive(s_lock);

    if (stale) {
        ESP_LOGI(TAG,
                 "semester version %lu already applied; replay ignored",
                 (unsigned long)parsed->version);
        free(parsed);
        return ESP_OK;
    }

    err = write_semester(parsed);

    if (err != ESP_OK) {
        free(parsed);
        return err;
    }

    /*
     * Different semester identity => clear old date-specific exceptions.
     * Same semester version update keeps current exceptions.
     */
    if (!same_identity) {
        err = write_atomic(TIMETABLE_OVERRIDES_PATH,
                           TIMETABLE_OVERRIDES_TMP_PATH,
                           "{\"type\":\"schedule_override_set\",\"version\":0,\"entries\":[]}");

        if (err != ESP_OK) {
            free(parsed);
            return err;
        }
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        free(parsed);
        return ESP_ERR_TIMEOUT;
    }

    *s_semester = *parsed;

    if (!same_identity) {
        memset(s_overrides, 0, sizeof(*s_overrides));
    }

    memset(&s_day, 0, sizeof(s_day));
    s_day_valid = false;

    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG,
             "semester updated %s start=%s v=%lu weeks=%u rules=%u",
             parsed->semester,
             parsed->start_date,
             (unsigned long)parsed->version,
             (unsigned)parsed->week_count,
             (unsigned)parsed->rule_count);

    free(parsed);
    return ESP_OK;
}

static esp_err_t import_override(cJSON *root)
{
    override_command_t cmd = {0};

    esp_err_t err = parse_override_command(root, &cmd);
    if (err != ESP_OK) {
        return err;
    }

    timetable_override_set_t *updated =
        (timetable_override_set_t *)timetable_calloc(sizeof(timetable_override_set_t));

    if (updated == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        free(updated);
        return ESP_ERR_TIMEOUT;
    }

    *updated = *s_overrides;
    xSemaphoreGive(s_lock);

    if (cmd.version <= updated->version) {
        ESP_LOGI(TAG,
                 "override version %lu stale/duplicate; current=%lu",
                 (unsigned long)cmd.version,
                 (unsigned long)updated->version);
        free(updated);
        return ESP_OK;
    }

    if (cmd.reset_all) {
        memset(updated, 0, sizeof(*updated));
        updated->version = cmd.version;
    } else {
        int index = find_override(updated,
                                  cmd.value.date,
                                  cmd.value.period);

        if (cmd.restore_one) {
            if (index >= 0) {
                remove_override(updated, (uint8_t)index);
            }
        } else if (index >= 0) {
            updated->entries[index] = cmd.value;
        } else {
            if (updated->count >= TIMETABLE_MAX_OVERRIDES) {
                free(updated);
                return ESP_ERR_INVALID_SIZE;
            }

            updated->entries[updated->count++] = cmd.value;
        }

        updated->version = cmd.version;

        qsort(updated->entries,
              updated->count,
              sizeof(updated->entries[0]),
              compare_override);
    }

    err = write_overrides(updated);

    if (err != ESP_OK) {
        free(updated);
        return err;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        free(updated);
        return ESP_ERR_TIMEOUT;
    }

    *s_overrides = *updated;
    memset(&s_day, 0, sizeof(s_day));
    s_day_valid = false;

    xSemaphoreGive(s_lock);

    if (cmd.reset_all) {
        ESP_LOGI(TAG,
                 "all overrides reset v=%lu",
                 (unsigned long)cmd.version);
    } else if (cmd.restore_one) {
        ESP_LOGI(TAG,
                 "override restored date=%s period=%u v=%lu",
                 cmd.value.date,
                 (unsigned)cmd.value.period,
                 (unsigned long)cmd.version);
    } else {
        ESP_LOGI(TAG,
                 "override %s date=%s period=%u v=%lu",
                 action_name(cmd.value.action),
                 cmd.value.date,
                 (unsigned)cmd.value.period,
                 (unsigned long)cmd.version);
    }

    free(updated);
    return ESP_OK;
}

esp_err_t timetable_import_json(const char *json, size_t len)
{
    if (!s_initialized || s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (json == NULL ||
        len == 0 ||
        len > TIMETABLE_MAX_IMPORT_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_ParseWithLength(json, len);

    if (root == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    esp_err_t err = ESP_ERR_INVALID_ARG;

    if (cJSON_IsString(type) && type->valuestring != NULL) {
        if (strcmp(type->valuestring, "semester_schedule") == 0) {
            err = import_semester(root);
        } else if (strcmp(type->valuestring, "schedule_override") == 0 ||
                   strcmp(type->valuestring, "schedule_override_reset") == 0) {
            err = import_override(root);
        }
    }

    cJSON_Delete(root);
    return err;
}

esp_err_t timetable_get_info(timetable_info_t *out)
{
    if (!s_initialized || s_lock == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memset(out, 0, sizeof(*out));

    copy_string(out->semester,
                sizeof(out->semester),
                s_semester->semester);
    copy_string(out->semester_start,
                sizeof(out->semester_start),
                s_semester->start_date);

    out->version = s_semester->version;
    out->override_version = s_overrides->version;
    out->week_count = s_semester->week_count;
    out->rule_count = s_semester->rule_count;
    out->override_count = s_overrides->count;

    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t timetable_get_day(timetable_day_t *out)
{
    if (!s_initialized || s_lock == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    time_t now = time(NULL);

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = build_today_locked(now);

    if (err == ESP_OK) {
        *out = s_day;
    }

    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t timetable_format_local_date(time_t now,
                                      char out[TIMETABLE_DATE_LEN])
{
    return format_date(now, out);
}

bool timetable_matches_date(time_t now)
{
    if (!s_initialized ||
        s_lock == NULL ||
        (int64_t)now < STORAGE_TIME_VALID_EPOCH) {
        return false;
    }

    char date[TIMETABLE_DATE_LEN] = {0};

    if (format_date(now, date) != ESP_OK) {
        return false;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return false;
    }

    esp_err_t err = build_today_locked(now);

    bool matched =
        err == ESP_OK &&
        s_day_valid &&
        strcmp(s_day.date, date) == 0;

    xSemaphoreGive(s_lock);
    return matched;
}

esp_err_t timetable_get_current_session(time_t now,
                                        timetable_entry_t *out)
{
    if (!s_initialized || s_lock == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((int64_t)now < STORAGE_TIME_VALID_EPOCH) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = build_today_locked(now);

    if (err != ESP_OK) {
        xSemaphoreGive(s_lock);
        return err;
    }

    struct tm local = {0};
    localtime_r(&now, &local);

    uint16_t minute =
        (uint16_t)(local.tm_hour * 60 + local.tm_min);

    for (uint8_t i = 0; i < s_day.count; ++i) {
        const timetable_entry_t *entry = &s_day.entries[i];

        if (minute >= entry->open_minute &&
            minute <= entry->end_minute) {
            *out = *entry;
            xSemaphoreGive(s_lock);
            return ESP_OK;
        }
    }

    xSemaphoreGive(s_lock);
    return ESP_ERR_NOT_FOUND;
}

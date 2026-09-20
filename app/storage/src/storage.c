#include "storage.h"
#include "nvs_cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "timetable.h"

static const char *TAG = "storage";
static bool s_initialized = false;
static uint32_t s_next_record_id = 1;
SemaphoreHandle_t g_fs_mutex = NULL;

extern const uint8_t attendance_config_default_json_start[] asm("_binary_attendance_config_default_json_start");
extern const uint8_t attendance_config_default_json_end[] asm("_binary_attendance_config_default_json_end");

static void attendance_config_defaults(attendance_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->start_minute = 7U * 60U + 30U;
    cfg->late_after_minute = 8U * 60U;
    cfg->end_minute = 9U * 60U;
    cfg->cooldown_sec = 60U;
    snprintf(cfg->timezone, sizeof(cfg->timezone), "%s", "CST-8");
    snprintf(cfg->ntp_server, sizeof(cfg->ntp_server), "%s", "ntp.aliyun.com");
    cfg->cloud_enabled = false;
    snprintf(cfg->mqtt_broker_uri, sizeof(cfg->mqtt_broker_uri), "%s", "mqtt://bemfa.com:9501");
    snprintf(cfg->bemfa_uid, sizeof(cfg->bemfa_uid), "%s", "REPLACE_WITH_YOUR_BEMFA_UID");
    snprintf(cfg->bemfa_topic, sizeof(cfg->bemfa_topic), "%s", "attendance004");
    cfg->mqtt_qos = 1;
    cfg->mqtt_retain = false;
    cfg->sync_batch_size = 8;
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
        hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return false;
    }
    *minutes_out = (uint16_t)(hour * 60 + minute);
    return true;
}

static esp_err_t ensure_attendance_config_file(void)
{
    /*
     * attendance_config.default.json is embedded in the application binary.
     *
     * The old implementation copied it only when /spiffs/attendance_config.json
     * did not exist. Therefore changing the JSON in the project and flashing a
     * new application did NOT update the persistent SPIFFS copy. This caused
     * cloud_enabled to remain at the value from the first boot.
     *
     * There is currently no runtime/GUI writer for attendance_config.json, so
     * the embedded project file is the authoritative configuration. Compare the
     * two files on boot and refresh only when their contents differ. Other SPIFFS
     * data (face DB, students, check-in log) is untouched.
     */
    size_t embedded_size =
        (size_t)(attendance_config_default_json_end - attendance_config_default_json_start);
    if (embedded_size > 0 &&
        attendance_config_default_json_start[embedded_size - 1] == '\0') {
        embedded_size--;
    }
    if (embedded_size == 0) {
        ESP_LOGE(TAG, "embedded attendance config is empty");
        return ESP_ERR_INVALID_SIZE;
    }

    bool file_exists = false;
    bool same_as_embedded = false;
    FILE *fp = fopen(ATTENDANCE_CONFIG_PATH, "rb");
    if (fp != NULL) {
        file_exists = true;
        if (fseek(fp, 0, SEEK_END) == 0) {
            long existing_size = ftell(fp);
            if (existing_size == (long)embedded_size &&
                fseek(fp, 0, SEEK_SET) == 0) {
                uint8_t *existing = (uint8_t *)malloc(embedded_size);
                if (existing != NULL) {
                    size_t read_size = fread(existing, 1, embedded_size, fp);
                    same_as_embedded =
                        read_size == embedded_size &&
                        memcmp(existing,
                               attendance_config_default_json_start,
                               embedded_size) == 0;
                    free(existing);
                }
            }
        }
        fclose(fp);
    }

    if (same_as_embedded) {
        return ESP_OK;
    }

    fp = fopen(ATTENDANCE_CONFIG_PATH, "wb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "open attendance config for write failed: %s",
                 ATTENDANCE_CONFIG_PATH);
        return ESP_FAIL;
    }

    size_t written =
        fwrite(attendance_config_default_json_start, 1, embedded_size, fp);
    fflush(fp);
    fclose(fp);
    if (written != embedded_size) {
        ESP_LOGE(TAG, "write attendance config failed: %u/%u bytes",
                 (unsigned)written, (unsigned)embedded_size);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "%s attendance config from embedded JSON: %s",
             file_exists ? "updated" : "created",
             ATTENDANCE_CONFIG_PATH);
    return ESP_OK;
}

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

static bool json_get_int(cJSON *root, const char *key, int *out)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    *out = item->valueint;
    return true;
}

static bool json_get_u32(cJSON *root, const char *key, uint32_t *out)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    double value = item->valuedouble;
    if (value < 0.0) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool json_get_i64(cJSON *root, const char *key, int64_t *out)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    *out = (int64_t)item->valuedouble;
    return true;
}

static bool json_get_string(cJSON *root, const char *key, char *out, size_t out_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    copy_string(out, out_size, item->valuestring);
    return true;
}

static bool parse_student_line(const char *line, student_profile_t *student)
{
    cJSON *root = cJSON_Parse(line);
    if (root == NULL) {
        return false;
    }

    student_profile_t tmp = {0};
    bool ok = json_get_int(root, "face_id", &tmp.face_id) &&
              json_get_string(root, "student_id", tmp.student_id, sizeof(tmp.student_id)) &&
              json_get_string(root, "name", tmp.name, sizeof(tmp.name)) &&
              json_get_int(root, "class_id", &tmp.class_id) &&
              json_get_i64(root, "created_ts", &tmp.created_ts);

    cJSON_Delete(root);
    if (ok && student != NULL) {
        *student = tmp;
    }
    return ok;
}

static bool parse_checkin_line(const char *line, checkin_record_t *record)
{
    cJSON *root = cJSON_Parse(line);
    if (root == NULL) {
        return false;
    }

    checkin_record_t tmp = {0};
    int status = 0;
    bool ok = json_get_u32(root, "record_id", &tmp.record_id) &&
              json_get_int(root, "face_id", &tmp.face_id) &&
              json_get_string(root, "student_id", tmp.student_id, sizeof(tmp.student_id)) &&
              json_get_string(root, "name", tmp.name, sizeof(tmp.name)) &&
              json_get_i64(root, "ts", &tmp.timestamp) &&
              json_get_int(root, "status", &status) &&
              json_get_int(root, "class_id", &tmp.class_id);

    /* New timetable fields are optional so existing JSONL logs stay readable. */
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "session_id");
    if (cJSON_IsNumber(item) && item->valuedouble >= 0) {
        tmp.session_id = (uint32_t)item->valuedouble;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "course_id");
    if (cJSON_IsNumber(item) && item->valueint >= 0 && item->valueint <= 65535) {
        tmp.course_id = (uint16_t)item->valueint;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "period");
    if (cJSON_IsNumber(item) && item->valueint >= 0 && item->valueint <= 255) {
        tmp.period = (uint8_t)item->valueint;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "course_name");
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        copy_string(tmp.course_name, sizeof(tmp.course_name), item->valuestring);
    }

    tmp.status = (checkin_status_t)status;
    cJSON_Delete(root);
    if (ok && record != NULL) {
        *record = tmp;
    }
    return ok;
}

static esp_err_t append_json_line_locked(const char *path, cJSON *root)
{
    char *json = cJSON_PrintUnformatted(root);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    FILE *fp = fopen(path, "a");
    if (fp == NULL) {
        cJSON_free(json);
        return ESP_FAIL;
    }

    int rc = fprintf(fp, "%s\n", json);
    fflush(fp);
    fclose(fp);
    cJSON_free(json);
    return (rc > 0) ? ESP_OK : ESP_FAIL;
}

static uint32_t scan_max_record_id_locked(void)
{
    FILE *fp = fopen(CHECKIN_LOG_PATH, "r");
    if (fp == NULL) {
        return 0;
    }

    char line[512];
    uint32_t max_id = 0;
    checkin_record_t rec;
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (parse_checkin_line(line, &rec) && rec.record_id > max_id) {
            max_id = rec.record_id;
        }
    }
    fclose(fp);
    return max_id;
}

const char *storage_checkin_status_str(checkin_status_t status)
{
    switch (status) {
        case CHECKIN_STATUS_NORMAL: return "normal";
        case CHECKIN_STATUS_LATE: return "late";
        case CHECKIN_STATUS_ABSENT: return "absent";
        case CHECKIN_STATUS_TIME_UNSYNCED: return "time_unsynced";
        default: return "unknown";
    }
}

esp_err_t storage_attendance_config_load(attendance_config_t *out)
{
    if (!s_initialized || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    attendance_config_t cfg;
    attendance_config_defaults(&cfg);
    *out = cfg;

    xSemaphoreTake(g_fs_mutex, portMAX_DELAY);
    esp_err_t err = ensure_attendance_config_file();
    if (err != ESP_OK) {
        xSemaphoreGive(g_fs_mutex);
        return err;
    }

    FILE *fp = fopen(ATTENDANCE_CONFIG_PATH, "r");
    if (fp == NULL) {
        xSemaphoreGive(g_fs_mutex);
        return ESP_FAIL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        xSemaphoreGive(g_fs_mutex);
        return ESP_FAIL;
    }
    long file_size = ftell(fp);
    if (file_size <= 0 || file_size > 4096 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        xSemaphoreGive(g_fs_mutex);
        return ESP_ERR_INVALID_SIZE;
    }

    char *json_text = (char *)malloc((size_t)file_size + 1U);
    if (json_text == NULL) {
        fclose(fp);
        xSemaphoreGive(g_fs_mutex);
        return ESP_ERR_NO_MEM;
    }
    size_t read_size = fread(json_text, 1, (size_t)file_size, fp);
    fclose(fp);
    xSemaphoreGive(g_fs_mutex);
    json_text[read_size] = '\0';
    if (read_size != (size_t)file_size) {
        free(json_text);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(json_text);
    free(json_text);
    if (root == NULL) {
        ESP_LOGE(TAG, "invalid attendance config JSON, using built-in defaults");
        *out = cfg;
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *timezone = cJSON_GetObjectItemCaseSensitive(root, "timezone");
    if (cJSON_IsString(timezone) && timezone->valuestring != NULL && timezone->valuestring[0] != '\0') {
        copy_string(cfg.timezone, sizeof(cfg.timezone), timezone->valuestring);
    }
    cJSON *ntp = cJSON_GetObjectItemCaseSensitive(root, "ntp_server");
    if (cJSON_IsString(ntp) && ntp->valuestring != NULL && ntp->valuestring[0] != '\0') {
        copy_string(cfg.ntp_server, sizeof(cfg.ntp_server), ntp->valuestring);
    }

    cJSON *schedule = cJSON_GetObjectItemCaseSensitive(root, "schedule");
    if (cJSON_IsObject(schedule)) {
        uint16_t start = cfg.start_minute;
        uint16_t late = cfg.late_after_minute;
        uint16_t end = cfg.end_minute;
        cJSON *item = cJSON_GetObjectItemCaseSensitive(schedule, "start");
        if (cJSON_IsString(item)) {
            (void)parse_hhmm(item->valuestring, &start);
        }
        item = cJSON_GetObjectItemCaseSensitive(schedule, "late_after");
        if (cJSON_IsString(item)) {
            (void)parse_hhmm(item->valuestring, &late);
        }
        item = cJSON_GetObjectItemCaseSensitive(schedule, "end");
        if (cJSON_IsString(item)) {
            (void)parse_hhmm(item->valuestring, &end);
        }
        if (start <= late && late <= end) {
            cfg.start_minute = start;
            cfg.late_after_minute = late;
            cfg.end_minute = end;
        } else {
            ESP_LOGW(TAG, "invalid attendance window, keep defaults");
        }
        item = cJSON_GetObjectItemCaseSensitive(schedule, "cooldown_sec");
        if (cJSON_IsNumber(item) && item->valueint >= 0 && item->valueint <= 3600) {
            cfg.cooldown_sec = (uint16_t)item->valueint;
        }
    }

    cJSON *bemfa = cJSON_GetObjectItemCaseSensitive(root, "bemfa");
    if (cJSON_IsObject(bemfa)) {
        cJSON *item = cJSON_GetObjectItemCaseSensitive(bemfa, "enabled");
        if (cJSON_IsBool(item)) {
            cfg.cloud_enabled = cJSON_IsTrue(item);
        }
        item = cJSON_GetObjectItemCaseSensitive(bemfa, "broker_uri");
        if (cJSON_IsString(item) && item->valuestring != NULL && item->valuestring[0] != '\0') {
            copy_string(cfg.mqtt_broker_uri, sizeof(cfg.mqtt_broker_uri), item->valuestring);
        }
        item = cJSON_GetObjectItemCaseSensitive(bemfa, "uid");
        if (cJSON_IsString(item) && item->valuestring != NULL) {
            copy_string(cfg.bemfa_uid, sizeof(cfg.bemfa_uid), item->valuestring);
        }
        item = cJSON_GetObjectItemCaseSensitive(bemfa, "topic");
        if (cJSON_IsString(item) && item->valuestring != NULL) {
            copy_string(cfg.bemfa_topic, sizeof(cfg.bemfa_topic), item->valuestring);
        }
        item = cJSON_GetObjectItemCaseSensitive(bemfa, "qos");
        if (cJSON_IsNumber(item) && (item->valueint == 0 || item->valueint == 1)) {
            cfg.mqtt_qos = (uint8_t)item->valueint;
        }
        item = cJSON_GetObjectItemCaseSensitive(bemfa, "retain");
        if (cJSON_IsBool(item)) {
            cfg.mqtt_retain = cJSON_IsTrue(item);
        }
        item = cJSON_GetObjectItemCaseSensitive(bemfa, "sync_batch_size");
        if (cJSON_IsNumber(item) && item->valueint >= 1 && item->valueint <= 16) {
            cfg.sync_batch_size = (uint8_t)item->valueint;
        }
    }

    cJSON_Delete(root);
    *out = cfg;

    ESP_LOGI(TAG,
             "attendance config loaded: cloud_enabled=%d broker=%s topic=%s "
             "window=%02u:%02u-%02u:%02u late_after=%02u:%02u",
             cfg.cloud_enabled ? 1 : 0,
             cfg.mqtt_broker_uri,
             cfg.bemfa_topic,
             (unsigned)(cfg.start_minute / 60U),
             (unsigned)(cfg.start_minute % 60U),
             (unsigned)(cfg.end_minute / 60U),
             (unsigned)(cfg.end_minute % 60U),
             (unsigned)(cfg.late_after_minute / 60U),
             (unsigned)(cfg.late_after_minute % 60U));
    return ESP_OK;
}

static esp_err_t recover_record_counter(void)
{
    xSemaphoreTake(g_fs_mutex, portMAX_DELAY);
    uint32_t max_id = scan_max_record_id_locked();
    xSemaphoreGive(g_fs_mutex);

    uint32_t next_id = 1;
    esp_err_t err = nvs_cfg_get_u32(STORAGE_NVS_KEY_NEXT_RECORD, 1, &next_id);
    if (err != ESP_OK) {
        return err;
    }
    if (next_id <= max_id) {
        next_id = max_id + 1;
        err = nvs_cfg_set_u32(STORAGE_NVS_KEY_NEXT_RECORD, next_id);
        if (err != ESP_OK) {
            return err;
        }
    }
    s_next_record_id = next_id;
    return ESP_OK;
}

esp_err_t storage_spiffs_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_vfs_spiffs_conf_t conf = {
        .base_path = STORAGE_BASE_PATH,
        .partition_label = "spiffs",
        .max_files = 8,
        .format_if_mount_failed = true,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_ERR_INVALID_STATE) {
        err = ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spiffs mount failed: %s", esp_err_to_name(err));
        return err;
    }

    if (g_fs_mutex == NULL) {
        g_fs_mutex = xSemaphoreCreateMutex();
        if (g_fs_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    size_t total = 0;
    size_t used = 0;
    if (esp_spiffs_info(conf.partition_label, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "spiffs total=%u KB used=%u KB", (unsigned)(total / 1024), (unsigned)(used / 1024));
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t storage_init(void)
{
    esp_err_t err = nvs_cfg_init();
    if (err != ESP_OK) {
        return err;
    }
    err = storage_spiffs_init();
    if (err != ESP_OK) {
        return err;
    }

    xSemaphoreTake(g_fs_mutex, portMAX_DELAY);
    err = ensure_attendance_config_file();
    xSemaphoreGive(g_fs_mutex);
    if (err != ESP_OK) {
        return err;
    }

    err = timetable_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "timetable init failed: %s", esp_err_to_name(err));
    }

    return recover_record_counter();
}

esp_err_t storage_student_find_by_face_id(int face_id, student_profile_t *out)
{
    if (!s_initialized || out == NULL || face_id <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    xSemaphoreTake(g_fs_mutex, portMAX_DELAY);
    FILE *fp = fopen(STUDENT_DB_PATH, "r");
    if (fp != NULL) {
        char line[512];
        student_profile_t item;
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (parse_student_line(line, &item) && item.face_id == face_id) {
                *out = item;
                ret = ESP_OK;
                break;
            }
        }
        fclose(fp);
    }
    xSemaphoreGive(g_fs_mutex);
    return ret;
}

esp_err_t storage_student_find_by_student_id(const char *student_id, student_profile_t *out)
{
    if (!s_initialized || student_id == NULL || student_id[0] == '\0' || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    xSemaphoreTake(g_fs_mutex, portMAX_DELAY);
    FILE *fp = fopen(STUDENT_DB_PATH, "r");
    if (fp != NULL) {
        char line[512];
        student_profile_t item;
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (parse_student_line(line, &item) && strcmp(item.student_id, student_id) == 0) {
                *out = item;
                ret = ESP_OK;
                break;
            }
        }
        fclose(fp);
    }
    xSemaphoreGive(g_fs_mutex);
    return ret;
}

esp_err_t storage_student_add(const student_profile_t *student)
{
    if (!s_initialized || student == NULL || student->face_id <= 0 ||
        student->student_id[0] == '\0' || student->name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    student_profile_t existing;
    if (storage_student_find_by_face_id(student->face_id, &existing) == ESP_OK ||
        storage_student_find_by_student_id(student->student_id, &existing) == ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(root, "face_id", student->face_id);
    cJSON_AddStringToObject(root, "student_id", student->student_id);
    cJSON_AddStringToObject(root, "name", student->name);
    cJSON_AddNumberToObject(root, "class_id", student->class_id);
    cJSON_AddNumberToObject(root, "created_ts", (double)student->created_ts);

    xSemaphoreTake(g_fs_mutex, portMAX_DELAY);
    esp_err_t err = append_json_line_locked(STUDENT_DB_PATH, root);
    xSemaphoreGive(g_fs_mutex);
    cJSON_Delete(root);
    return err;
}

int storage_student_list(student_profile_t *out, int max_count)
{
    if (!s_initialized || out == NULL || max_count <= 0) {
        return 0;
    }

    int count = 0;
    xSemaphoreTake(g_fs_mutex, portMAX_DELAY);
    FILE *fp = fopen(STUDENT_DB_PATH, "r");
    if (fp != NULL) {
        char line[512];
        while (count < max_count && fgets(line, sizeof(line), fp) != NULL) {
            if (parse_student_line(line, &out[count])) {
                count++;
            }
        }
        fclose(fp);
    }
    xSemaphoreGive(g_fs_mutex);
    return count;
}

esp_err_t storage_checkin_add(checkin_record_t *record)
{
    if (!s_initialized || record == NULL || record->student_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(g_fs_mutex, portMAX_DELAY);

    if (record->record_id == 0) {
        record->record_id = s_next_record_id;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        xSemaphoreGive(g_fs_mutex);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(root, "record_id", record->record_id);
    cJSON_AddNumberToObject(root, "session_id", record->session_id);
    cJSON_AddNumberToObject(root, "face_id", record->face_id);
    cJSON_AddStringToObject(root, "student_id", record->student_id);
    cJSON_AddStringToObject(root, "name", record->name);
    cJSON_AddNumberToObject(root, "ts", (double)record->timestamp);
    cJSON_AddNumberToObject(root, "status", (int)record->status);
    cJSON_AddNumberToObject(root, "class_id", record->class_id);
    cJSON_AddNumberToObject(root, "course_id", record->course_id);
    cJSON_AddNumberToObject(root, "period", record->period);
    cJSON_AddStringToObject(root, "course_name", record->course_name);

    esp_err_t err = append_json_line_locked(CHECKIN_LOG_PATH, root);
    cJSON_Delete(root);
    if (err == ESP_OK && record->record_id >= s_next_record_id) {
        s_next_record_id = record->record_id + 1;
        esp_err_t nvs_err = nvs_cfg_set_u32(STORAGE_NVS_KEY_NEXT_RECORD, s_next_record_id);
        if (nvs_err != ESP_OK) {
            // The JSONL record is already durable. Boot-time recovery scans the log and repairs NVS.
            ESP_LOGW(TAG, "persist next record id failed: %s", esp_err_to_name(nvs_err));
        }
    }

    xSemaphoreGive(g_fs_mutex);
    return err;
}

esp_err_t storage_append_checkin(const checkin_record_t *record)
{
    if (record == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    checkin_record_t copy = *record;
    return storage_checkin_add(&copy);
}

esp_err_t storage_get_last_checkin_for_student(const char *student_id, checkin_record_t *out)
{
    if (!s_initialized || student_id == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    bool found = false;
    checkin_record_t latest = {0};
    xSemaphoreTake(g_fs_mutex, portMAX_DELAY);
    FILE *fp = fopen(CHECKIN_LOG_PATH, "r");
    if (fp != NULL) {
        char line[512];
        checkin_record_t item;
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (parse_checkin_line(line, &item) && strcmp(item.student_id, student_id) == 0) {
                latest = item;
                found = true;
            }
        }
        fclose(fp);
    }
    xSemaphoreGive(g_fs_mutex);

    if (!found) {
        return ESP_ERR_NOT_FOUND;
    }
    *out = latest;
    return ESP_OK;
}

static bool timestamp_is_today(int64_t timestamp)
{
    time_t now = time(NULL);
    if ((int64_t)now < STORAGE_TIME_VALID_EPOCH ||
        timestamp < STORAGE_TIME_VALID_EPOCH) {
        return false;
    }

    time_t value = (time_t)timestamp;
    struct tm now_local = {0};
    struct tm value_local = {0};
    localtime_r(&now, &now_local);
    localtime_r(&value, &value_local);
    return now_local.tm_year == value_local.tm_year &&
           now_local.tm_yday == value_local.tm_yday;
}

esp_err_t storage_get_checkin_for_student_session(const char *student_id,
                                                  uint32_t session_id,
                                                  checkin_record_t *out)
{
    if (!s_initialized || student_id == NULL || student_id[0] == '\0' ||
        session_id == 0 || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    bool found = false;
    checkin_record_t latest = {0};
    xSemaphoreTake(g_fs_mutex, portMAX_DELAY);
    FILE *fp = fopen(CHECKIN_LOG_PATH, "r");
    if (fp != NULL) {
        char line[512];
        checkin_record_t item;
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (parse_checkin_line(line, &item) &&
                item.session_id == session_id &&
                strcmp(item.student_id, student_id) == 0) {
                latest = item;
                found = true;
            }
        }
        fclose(fp);
    }
    xSemaphoreGive(g_fs_mutex);

    if (!found) {
        return ESP_ERR_NOT_FOUND;
    }
    *out = latest;
    return ESP_OK;
}

int storage_checkin_list_page_today(checkin_record_t *out,
                                    int max_count,
                                    uint32_t offset_from_newest,
                                    uint32_t *total_count_out)
{
    if (!s_initialized || out == NULL || max_count <= 0) {
        if (total_count_out != NULL) {
            *total_count_out = 0;
        }
        return 0;
    }

    int result_count = 0;
    uint32_t total = 0;

    xSemaphoreTake(g_fs_mutex, portMAX_DELAY);

    FILE *fp = fopen(CHECKIN_LOG_PATH, "r");
    if (fp != NULL) {
        char line[512];
        checkin_record_t item;
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (parse_checkin_line(line, &item) && timestamp_is_today(item.timestamp)) {
                total++;
            }
        }
        fclose(fp);
    }

    if (total_count_out != NULL) {
        *total_count_out = total;
    }

    if (offset_from_newest < total) {
        uint32_t end_exclusive = total - offset_from_newest;
        uint32_t start_index = end_exclusive > (uint32_t)max_count
                                   ? end_exclusive - (uint32_t)max_count
                                   : 0;

        fp = fopen(CHECKIN_LOG_PATH, "r");
        if (fp != NULL) {
            char line[512];
            checkin_record_t item;
            uint32_t today_index = 0;
            while (fgets(line, sizeof(line), fp) != NULL) {
                if (!parse_checkin_line(line, &item) || !timestamp_is_today(item.timestamp)) {
                    continue;
                }
                if (today_index >= start_index &&
                    today_index < end_exclusive &&
                    result_count < max_count) {
                    out[result_count++] = item;
                }
                today_index++;
                if (today_index >= end_exclusive) {
                    break;
                }
            }
            fclose(fp);
        }
    }

    xSemaphoreGive(g_fs_mutex);

    for (int i = 0; i < result_count / 2; ++i) {
        checkin_record_t tmp = out[i];
        out[i] = out[result_count - 1 - i];
        out[result_count - 1 - i] = tmp;
    }
    return result_count;
}

esp_err_t storage_get_stats_today(attendance_stats_t *out)
{
    if (!s_initialized || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out = (attendance_stats_t){0};

    timetable_day_t day = {0};
    bool schedule_today = false;
    uint16_t current_minute = 0;
    time_t now = time(NULL);
    if ((int64_t)now >= STORAGE_TIME_VALID_EPOCH &&
        timetable_get_day(&day) == ESP_OK &&
        timetable_matches_date(now)) {
        struct tm local = {0};
        localtime_r(&now, &local);
        current_minute = (uint16_t)(local.tm_hour * 60 + local.tm_min);
        schedule_today = true;
    }

    uint16_t expected[TIMETABLE_MAX_SESSIONS] = {0};
    uint16_t present[TIMETABLE_MAX_SESSIONS] = {0};

    xSemaphoreTake(g_fs_mutex, portMAX_DELAY);

    FILE *student_fp = fopen(STUDENT_DB_PATH, "r");
    if (student_fp != NULL) {
        char student_line[256];
        student_profile_t student;
        while (fgets(student_line, sizeof(student_line), student_fp) != NULL) {
            if (!parse_student_line(student_line, &student)) {
                continue;
            }
            out->student_count++;

            if (schedule_today) {
                for (uint8_t i = 0; i < day.count; ++i) {
                    const timetable_entry_t *entry = &day.entries[i];
                    if (entry->end_minute < current_minute &&
                        (entry->class_id == 0 || entry->class_id == student.class_id)) {
                        expected[i]++;
                    }
                }
            }
        }
        fclose(student_fp);
    }

    FILE *fp = fopen(CHECKIN_LOG_PATH, "r");
    if (fp != NULL) {
        char line[512];
        checkin_record_t item;
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (!parse_checkin_line(line, &item) || !timestamp_is_today(item.timestamp)) {
                continue;
            }

            out->checkin_count++;
            if (item.status == CHECKIN_STATUS_LATE) {
                out->late_count++;
            } else if (item.status == CHECKIN_STATUS_ABSENT) {
                /* Legacy explicit absent records remain countable. */
                out->absent_count++;
            } else if (item.status == CHECKIN_STATUS_TIME_UNSYNCED) {
                out->unsynced_count++;
            } else {
                out->normal_count++;
            }

            if (schedule_today && item.session_id != 0) {
                for (uint8_t i = 0; i < day.count; ++i) {
                    if (day.entries[i].session_id == item.session_id &&
                        day.entries[i].end_minute < current_minute) {
                        present[i]++;
                        break;
                    }
                }
            }
        }
        fclose(fp);
    }

    xSemaphoreGive(g_fs_mutex);

    /* Only completed sessions contribute derived absences. */
    if (schedule_today) {
        for (uint8_t i = 0; i < day.count; ++i) {
            if (day.entries[i].end_minute < current_minute && expected[i] > present[i]) {
                out->absent_count += (uint32_t)(expected[i] - present[i]);
            }
        }
    }

    return ESP_OK;
}

int storage_checkin_list_recent(checkin_record_t *out, int max_count)
{
    if (!s_initialized || out == NULL || max_count <= 0) {
        return 0;
    }

    int count = 0;
    xSemaphoreTake(g_fs_mutex, portMAX_DELAY);
    FILE *fp = fopen(CHECKIN_LOG_PATH, "r");
    if (fp != NULL) {
        char line[512];
        checkin_record_t item;
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (!parse_checkin_line(line, &item)) {
                continue;
            }
            if (count < max_count) {
                out[count++] = item;
            } else {
                memmove(&out[0], &out[1], (size_t)(max_count - 1) * sizeof(out[0]));
                out[max_count - 1] = item;
            }
        }
        fclose(fp);
    }
    xSemaphoreGive(g_fs_mutex);

    for (int i = 0; i < count / 2; ++i) {
        checkin_record_t tmp = out[i];
        out[i] = out[count - 1 - i];
        out[count - 1 - i] = tmp;
    }
    return count;
}

int storage_checkin_list_page(checkin_record_t *out,
                              int max_count,
                              uint32_t offset_from_newest,
                              uint32_t *total_count_out)
{
    if (!s_initialized || out == NULL || max_count <= 0) {
        if (total_count_out != NULL) {
            *total_count_out = 0;
        }
        return 0;
    }

    int result_count = 0;
    uint32_t total = 0;

    xSemaphoreTake(g_fs_mutex, portMAX_DELAY);

    /*
     * First pass: count valid JSONL records. Keeping pagination in storage
     * avoids allocating all attendance records in the LVGL task.
     */
    FILE *fp = fopen(CHECKIN_LOG_PATH, "r");
    if (fp != NULL) {
        char line[512];
        checkin_record_t item;
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (parse_checkin_line(line, &item)) {
                total++;
            }
        }
        fclose(fp);
    }

    if (total_count_out != NULL) {
        *total_count_out = total;
    }

    if (offset_from_newest < total) {
        uint32_t end_exclusive = total - offset_from_newest;
        uint32_t start_index =
            end_exclusive > (uint32_t)max_count
                ? end_exclusive - (uint32_t)max_count
                : 0;

        fp = fopen(CHECKIN_LOG_PATH, "r");
        if (fp != NULL) {
            char line[512];
            checkin_record_t item;
            uint32_t valid_index = 0;

            while (fgets(line, sizeof(line), fp) != NULL) {
                if (!parse_checkin_line(line, &item)) {
                    continue;
                }

                if (valid_index >= start_index &&
                    valid_index < end_exclusive &&
                    result_count < max_count) {
                    out[result_count++] = item;
                }

                valid_index++;
                if (valid_index >= end_exclusive) {
                    break;
                }
            }
            fclose(fp);
        }
    }

    xSemaphoreGive(g_fs_mutex);

    /* File order is oldest->newest; GUI pages are newest->oldest. */
    for (int i = 0; i < result_count / 2; ++i) {
        checkin_record_t tmp = out[i];
        out[i] = out[result_count - 1 - i];
        out[result_count - 1 - i] = tmp;
    }

    return result_count;
}

esp_err_t storage_get_stats(attendance_stats_t *out)
{
    if (!s_initialized || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out = (attendance_stats_t){0};

    xSemaphoreTake(g_fs_mutex, portMAX_DELAY);

    FILE *student_fp = fopen(STUDENT_DB_PATH, "r");
    if (student_fp != NULL) {
        char student_line[256];
        student_profile_t student;
        while (fgets(student_line, sizeof(student_line), student_fp) != NULL) {
            if (parse_student_line(student_line, &student)) {
                out->student_count++;
            }
        }
        fclose(student_fp);
    }

    FILE *fp = fopen(CHECKIN_LOG_PATH, "r");
    if (fp != NULL) {
        char line[512];
        checkin_record_t item;
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (!parse_checkin_line(line, &item)) {
                continue;
            }
            out->checkin_count++;
            if (item.status == CHECKIN_STATUS_LATE) {
                out->late_count++;
            } else if (item.status == CHECKIN_STATUS_ABSENT) {
                out->absent_count++;
            } else if (item.status == CHECKIN_STATUS_TIME_UNSYNCED) {
                out->unsynced_count++;
            } else {
                out->normal_count++;
            }
        }
        fclose(fp);
    }
    xSemaphoreGive(g_fs_mutex);
    return ESP_OK;
}

int storage_read_unuploaded_records(checkin_record_t *out, int max_count, uint32_t last_uploaded_id)
{
    if (!s_initialized || out == NULL || max_count <= 0) {
        return 0;
    }

    int count = 0;
    xSemaphoreTake(g_fs_mutex, portMAX_DELAY);
    FILE *fp = fopen(CHECKIN_LOG_PATH, "r");
    if (fp != NULL) {
        char line[512];
        checkin_record_t item;
        while (count < max_count && fgets(line, sizeof(line), fp) != NULL) {
            if (parse_checkin_line(line, &item) && item.record_id > last_uploaded_id) {
                out[count++] = item;
            }
        }
        fclose(fp);
    }
    xSemaphoreGive(g_fs_mutex);
    return count;
}

esp_err_t storage_get_upload_mark(uint32_t *record_id_out)
{
    if (record_id_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return nvs_cfg_get_u32(STORAGE_NVS_KEY_UPLOAD_MARK, 0, record_id_out);
}

esp_err_t storage_set_upload_mark(uint32_t record_id)
{
    return nvs_cfg_set_u32(STORAGE_NVS_KEY_UPLOAD_MARK, record_id);
}

#include "cloud_service.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "storage.h"
#include "timetable.h"
#include "wifi_provisioning.h"

#define CLOUD_MQTT_CONNECTED_BIT BIT0
#define CLOUD_MAX_SYNC_BATCH      16
#define CLOUD_PUBACK_WARN_MS      15000
#define CLOUD_PUBACK_RETRY_MS     120000
#define CLOUD_POLL_MS             1000
#define CLOUD_NETWORK_START_DELAY_MS 5000
#define CLOUD_SNTP_RETRY_MS          12000
#define CLOUD_SNTP_SERVER_COUNT      3
#define CLOUD_SCHEDULE_TOPIC_SUFFIX  "schedule"

static const char *TAG = "cloud_service";

static attendance_config_t s_cfg;
static esp_mqtt_client_handle_t s_mqtt = NULL;
static EventGroupHandle_t s_event_group = NULL;
static QueueHandle_t s_puback_queue = NULL;
static SemaphoreHandle_t s_status_lock = NULL;
static cloud_service_status_t s_status;
static bool s_started = false;
static bool s_sntp_started = false;
static uint8_t s_sntp_server_index = 0;
static TickType_t s_sntp_last_start_tick = 0;
static bool s_time_sync_logged = false;

/*
 * Exactly one application-level QoS1 attendance publish may be in flight.
 *
 * ESP-MQTT already owns retransmission of a QoS1 packet while it is in the
 * client's outbox. Re-publishing the same local record every cloud loop before
 * the PUBACK arrives creates duplicate cloud messages. Keep the record pending
 * until MQTT_EVENT_PUBLISHED acknowledges the exact msg_id.
 */
static int s_inflight_msg_id = -1;
static uint32_t s_inflight_record_id = 0;
static TickType_t s_inflight_since = 0;
static bool s_inflight_warned = false;

/* Fragment-safe buffer used only while receiving one MQTT schedule command. */
static char *s_rx_payload = NULL;
static int s_rx_total = 0;
static int s_rx_received = 0;
static bool s_rx_schedule_topic = false;
static char s_schedule_topic[STORAGE_BEMFA_TOPIC_LEN + 16];

static const wifi_provisioning_config_t s_wifi_cfg = {
    .ap_ssid_prefix = "FaceAttend",
    .ap_password = "88888888",
    .sta_max_retry = 5,
    .stop_ap_after_connected = true,
};

static void status_update(bool wifi_connected,
                          bool mqtt_connected,
                          bool time_synced,
                          uint32_t last_uploaded_id,
                          uint32_t pending_batch_count,
                          esp_err_t last_error)
{
    if (s_status_lock == NULL) {
        return;
    }
    if (xSemaphoreTake(s_status_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    s_status.wifi_connected = wifi_connected;
    s_status.mqtt_connected = mqtt_connected;
    s_status.time_synced = time_synced;
    s_status.cloud_enabled = s_cfg.cloud_enabled;
    s_status.last_uploaded_id = last_uploaded_id;
    s_status.pending_batch_count = pending_batch_count;
    s_status.last_error = last_error;
    xSemaphoreGive(s_status_lock);
}

static bool system_time_valid(void)
{
    return (int64_t)time(NULL) >= STORAGE_TIME_VALID_EPOCH;
}

static bool valid_topic(const char *topic)
{
    if (topic == NULL) {
        return false;
    }
    size_t len = strlen(topic);
    if (len == 0 || len >= STORAGE_BEMFA_TOPIC_LEN) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        if (!isalnum((unsigned char)topic[i])) {
            return false;
        }
    }
    return true;
}

static bool cloud_credentials_valid(void)
{
    return s_cfg.cloud_enabled &&
           s_cfg.bemfa_uid[0] != '\0' &&
           strstr(s_cfg.bemfa_uid, "REPLACE_WITH_") == NULL &&
           valid_topic(s_cfg.bemfa_topic) &&
           s_cfg.mqtt_broker_uri[0] != '\0';
}

static esp_err_t prepare_schedule_topic(void)
{
    int n = snprintf(s_schedule_topic,
                     sizeof(s_schedule_topic),
                     "%s%s",
                     s_cfg.bemfa_topic,
                     CLOUD_SCHEDULE_TOPIC_SUFFIX);

    if (n < 0 ||
        (size_t)n >= sizeof(s_schedule_topic)) {
        s_schedule_topic[0] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }

    /*
     * Example:
     *   attendance topic: attendance004
     *   schedule topic:   attendance004schedule
     *
     * The suffix is alphanumeric so it remains compatible with the
     * project's conservative BaFa topic-name validation.
     */
    return ESP_OK;
}

static char *schedule_payload_alloc(size_t size)
{
    char *buffer =
        (char *)heap_caps_malloc(
            size,
            MALLOC_CAP_SPIRAM |
                MALLOC_CAP_8BIT);

    if (buffer == NULL) {
        buffer = (char *)malloc(size);
    }

    return buffer;
}

static void log_internal_heap(const char *phase)
{
    size_t internal_free =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t internal_largest =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t dma_free =
        heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    size_t dma_largest =
        heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

    ESP_LOGI(TAG,
             "%s heap: internal_free=%u internal_largest=%u dma_free=%u dma_largest=%u",
             phase,
             (unsigned)internal_free,
             (unsigned)internal_largest,
             (unsigned)dma_free,
             (unsigned)dma_largest);
}

static esp_err_t start_wifi_service(void)
{
    log_internal_heap("before Wi-Fi init");

    esp_err_t err = wifi_provisioning_start(&s_wifi_cfg);
    if (err != ESP_OK) {
        log_internal_heap("Wi-Fi init failed");
        return err;
    }

    /* Avoid STA modem-sleep transitions while the parallel camera is active. */
    esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ps_err != ESP_OK) {
        ESP_LOGW(TAG, "disable Wi-Fi power save failed: %s",
                 esp_err_to_name(ps_err));
    }

    log_internal_heap("after Wi-Fi init");
    return ESP_OK;
}

static size_t json_escape(const char *src, char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return 0;
    }
    size_t used = 0;
    dst[0] = '\0';
    if (src == NULL) {
        return 0;
    }

    for (const unsigned char *p = (const unsigned char *)src; *p != '\0'; ++p) {
        char escaped[7] = {0};
        const char *chunk = NULL;
        size_t chunk_len = 0;
        switch (*p) {
            case '"': chunk = "\\\""; chunk_len = 2; break;
            case '\\': chunk = "\\\\"; chunk_len = 2; break;
            case '\b': chunk = "\\b"; chunk_len = 2; break;
            case '\f': chunk = "\\f"; chunk_len = 2; break;
            case '\n': chunk = "\\n"; chunk_len = 2; break;
            case '\r': chunk = "\\r"; chunk_len = 2; break;
            case '\t': chunk = "\\t"; chunk_len = 2; break;
            default:
                if (*p < 0x20) {
                    snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned)*p);
                    chunk = escaped;
                    chunk_len = 6;
                }
                break;
        }

        if (chunk != NULL) {
            if (used + chunk_len + 1 > dst_size) {
                break;
            }
            memcpy(dst + used, chunk, chunk_len);
            used += chunk_len;
        } else {
            if (used + 2 > dst_size) {
                break;
            }
            dst[used++] = (char)*p;
        }
    }
    dst[used] = '\0';
    return used;
}

static void format_local_time(int64_t timestamp, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }
    if (timestamp < STORAGE_TIME_VALID_EPOCH) {
        snprintf(out, out_size, "%s", "time-not-synced");
        return;
    }

    time_t value = (time_t)timestamp;
    struct tm local = {0};
    localtime_r(&value, &local);
    if (strftime(out, out_size, "%Y-%m-%dT%H:%M:%S%z", &local) == 0) {
        snprintf(out, out_size, "%lld", (long long)timestamp);
    }
}

static int build_record_payload(const checkin_record_t *record,
                                char *payload,
                                size_t payload_size)
{
    if (record == NULL || payload == NULL || payload_size == 0) {
        return -1;
    }

    char student_id[STORAGE_STUDENT_ID_LEN * 2 + 8];
    char name[STORAGE_STUDENT_NAME_LEN * 2 + 8];
    char course_name[STORAGE_COURSE_NAME_LEN * 2 + 8];
    char local_time[48];
    json_escape(record->student_id, student_id, sizeof(student_id));
    json_escape(record->name, name, sizeof(name));
    json_escape(record->course_name, course_name, sizeof(course_name));
    format_local_time(record->timestamp, local_time, sizeof(local_time));

    const bool time_valid = record->timestamp >= STORAGE_TIME_VALID_EPOCH;
    const checkin_status_t effective_status =
        time_valid ? record->status : CHECKIN_STATUS_TIME_UNSYNCED;

    int n = snprintf(payload, payload_size,
                     "{\"type\":\"attendance\",\"record_id\":%lu,"
                     "\"session_id\":%lu,\"course_id\":%u,"
                     "\"course_name\":\"%s\",\"period\":%u,"
                     "\"student_id\":\"%s\",\"name\":\"%s\","
                     "\"class_id\":%d,\"face_id\":%d,"
                     "\"timestamp\":%lld,\"time\":\"%s\","
                     "\"time_synced\":%s,\"status\":\"%s\"}",
                     (unsigned long)record->record_id,
                     (unsigned long)record->session_id,
                     (unsigned)record->course_id,
                     course_name,
                     (unsigned)record->period,
                     student_id,
                     name,
                     record->class_id,
                     record->face_id,
                     (long long)record->timestamp,
                     local_time,
                     time_valid ? "true" : "false",
                     storage_checkin_status_str(effective_status));
    if (n < 0 || (size_t)n >= payload_size) {
        return -1;
    }
    return n;
}

static void mqtt_rx_reset(void)
{
    free(s_rx_payload);
    s_rx_payload = NULL;
    s_rx_total = 0;
    s_rx_received = 0;
    s_rx_schedule_topic = false;
}

static bool mqtt_topic_is_schedule_command(const esp_mqtt_event_handle_t event)
{
    size_t expected = strlen(s_schedule_topic);

    return expected > 0 &&
           event->topic != NULL &&
           event->topic_len == (int)expected &&
           memcmp(event->topic,
                  s_schedule_topic,
                  expected) == 0;
}

static void handle_mqtt_data(esp_mqtt_event_handle_t event)
{
    if (event == NULL || event->data == NULL || event->data_len <= 0) {
        return;
    }

    if (event->current_data_offset == 0) {
        mqtt_rx_reset();
        s_rx_schedule_topic = mqtt_topic_is_schedule_command(event);
        s_rx_total = event->total_data_len;

        if (!s_rx_schedule_topic) {
            return;
        }
        if (s_rx_total <= 0 || s_rx_total > TIMETABLE_MAX_IMPORT_BYTES) {
            ESP_LOGW(TAG, "ignore schedule command size=%d", s_rx_total);
            s_rx_schedule_topic = false;
            return;
        }

        s_rx_payload = schedule_payload_alloc((size_t)s_rx_total + 1U);
        if (s_rx_payload == NULL) {
            ESP_LOGE(TAG, "allocate schedule command failed size=%d", s_rx_total);
            s_rx_schedule_topic = false;
            return;
        }
    }

    if (!s_rx_schedule_topic || s_rx_payload == NULL) {
        return;
    }

    int offset = event->current_data_offset;
    if (offset < 0 || offset + event->data_len > s_rx_total) {
        ESP_LOGW(TAG, "invalid schedule MQTT fragment offset=%d len=%d total=%d",
                 offset, event->data_len, s_rx_total);
        mqtt_rx_reset();
        return;
    }

    memcpy(s_rx_payload + offset, event->data, (size_t)event->data_len);
    int received_end = offset + event->data_len;
    if (received_end > s_rx_received) {
        s_rx_received = received_end;
    }

    if (s_rx_received < s_rx_total) {
        return;
    }

    s_rx_payload[s_rx_total] = '\0';

    esp_err_t err =
        timetable_import_json(s_rx_payload,
                              (size_t)s_rx_total);

    if (err == ESP_OK) {
        timetable_info_t info = {0};

        if (timetable_get_info(&info) == ESP_OK) {
            ESP_LOGI(
                TAG,
                "schedule config accepted: semester=%s version=%lu "
                "override_v=%lu rules=%u overrides=%u",
                info.semester[0] != '\0'
                    ? info.semester
                    : "(none)",
                (unsigned long)info.version,
                (unsigned long)info.override_version,
                (unsigned)info.rule_count,
                (unsigned)info.override_count);
        }
    } else {
        ESP_LOGW(TAG,
                 "invalid schedule command on topic %s: %s",
                 s_schedule_topic,
                 esp_err_to_name(err));
    }

    mqtt_rx_reset();
}

static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED: {
            xEventGroupSetBits(
                s_event_group,
                CLOUD_MQTT_CONNECTED_BIT);

            ESP_LOGI(TAG,
                     "MQTT connected to BaFa Cloud");

            if (s_schedule_topic[0] != '\0') {
                int sub_id =
                    esp_mqtt_client_subscribe(
                        s_mqtt,
                        s_schedule_topic,
                        1);

                if (sub_id >= 0) {
                    ESP_LOGI(
                        TAG,
                        "subscribed schedule topic=%s msg_id=%d",
                        s_schedule_topic,
                        sub_id);
                } else {
                    ESP_LOGW(
                        TAG,
                        "subscribe schedule topic failed: %s",
                        s_schedule_topic);
                }
            }
            break;
        }
        case MQTT_EVENT_DISCONNECTED:
            xEventGroupClearBits(s_event_group, CLOUD_MQTT_CONNECTED_BIT);
            mqtt_rx_reset();
            ESP_LOGW(TAG, "MQTT disconnected");
            break;
        case MQTT_EVENT_PUBLISHED:
            if (s_puback_queue != NULL) {
                int msg_id = event->msg_id;
                (void)xQueueSend(s_puback_queue, &msg_id, 0);
            }
            break;
        case MQTT_EVENT_DATA:
            handle_mqtt_data(event);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGW(TAG, "MQTT transport/protocol error");
            break;
        default:
            break;
    }
}

static esp_err_t start_mqtt(void)
{
    if (s_mqtt != NULL) {
        return ESP_OK;
    }
    if (!cloud_credentials_valid()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t topic_err = prepare_schedule_topic();
    if (topic_err != ESP_OK) {
        ESP_LOGE(TAG,
                 "prepare schedule topic failed: %s",
                 esp_err_to_name(topic_err));
        return topic_err;
    }

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = s_cfg.mqtt_broker_uri,
        .credentials.client_id = s_cfg.bemfa_uid,
        .session.keepalive = 60,
        .session.protocol_ver = MQTT_PROTOCOL_V_3_1_1,
        .network.reconnect_timeout_ms = 5000,
        .task.priority = 2,
        .task.stack_size = 6144,
        .buffer.size = 1024,
        .buffer.out_size = 1024,
    };

    s_mqtt = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(s_mqtt);
        s_mqtt = NULL;
        return err;
    }
    err = esp_mqtt_client_start(s_mqtt);
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(s_mqtt);
        s_mqtt = NULL;
        return err;
    }
    return ESP_OK;
}

static const char *sntp_server_at(uint8_t index)
{
    switch (index % CLOUD_SNTP_SERVER_COUNT) {
        case 0:
            return s_cfg.ntp_server[0] != '\0'
                       ? s_cfg.ntp_server
                       : "ntp.aliyun.com";
        case 1:
            return "time1.cloud.tencent.com";
        case 2:
        default:
            return "pool.ntp.org";
    }
}

static void sntp_sync_cb(struct timeval *tv)
{
    if (tv == NULL) {
        return;
    }

    time_t value = (time_t)tv->tv_sec;
    struct tm local = {0};
    char text[40] = {0};
    localtime_r(&value, &local);
    if (strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local) == 0) {
        snprintf(text, sizeof(text), "%lld", (long long)tv->tv_sec);
    }

    ESP_LOGI(TAG, "SNTP synchronized: %s (%s)", text, s_cfg.timezone);
    s_time_sync_logged = true;
}

static esp_err_t restart_sntp(uint8_t server_index)
{
    const char *server = sntp_server_at(server_index);

    if (s_sntp_started) {
        esp_netif_sntp_deinit();
        s_sntp_started = false;
    }

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(server);
    config.sync_cb = sntp_sync_cb;
    config.smooth_sync = false;
    config.wait_for_sync = false;
    config.start = true;

    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP init failed for %s: %s",
                 server, esp_err_to_name(err));
        return err;
    }

    s_sntp_started = true;
    s_sntp_server_index = server_index % CLOUD_SNTP_SERVER_COUNT;
    s_sntp_last_start_tick = xTaskGetTickCount();

    ESP_LOGI(TAG, "SNTP query started: server=%s timezone=%s",
             server, s_cfg.timezone);
    return ESP_OK;
}

static void maintain_sntp(void)
{
    if (system_time_valid()) {
        if (!s_time_sync_logged) {
            time_t now = time(NULL);
            struct tm local = {0};
            char text[40] = {0};
            localtime_r(&now, &local);
            if (strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local) == 0) {
                snprintf(text, sizeof(text), "%lld", (long long)now);
            }
            ESP_LOGI(TAG, "system time valid: %s (%s)",
                     text, s_cfg.timezone);
            s_time_sync_logged = true;
        }
        return;
    }

    if (!s_sntp_started) {
        (void)restart_sntp(0);
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if ((now - s_sntp_last_start_tick) >=
        pdMS_TO_TICKS(CLOUD_SNTP_RETRY_MS)) {
        uint8_t next =
            (uint8_t)((s_sntp_server_index + 1U) % CLOUD_SNTP_SERVER_COUNT);
        ESP_LOGW(TAG, "time not synchronized yet, retrying SNTP with %s",
                 sntp_server_at(next));
        (void)restart_sntp(next);
    }
}

static esp_err_t persist_upload_mark(uint32_t record_id,
                                     uint32_t *last_uploaded_inout)
{
    /*
     * Advance the in-memory mark first. If NVS persistence ever fails, do not
     * hammer the same cloud record repeatedly during this boot. Boot-time
     * recovery can resend it once, and the mini-program also deduplicates by
     * record_id.
     */
    if (last_uploaded_inout != NULL && record_id > *last_uploaded_inout) {
        *last_uploaded_inout = record_id;
    }

    esp_err_t err = storage_set_upload_mark(record_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "persist upload mark failed record=%lu: %s",
                 (unsigned long)record_id,
                 esp_err_to_name(err));
    }
    return err;
}

static bool consume_inflight_puback(uint32_t *last_uploaded_inout,
                                   esp_err_t *persist_error_out)
{
    if (persist_error_out != NULL) {
        *persist_error_out = ESP_OK;
    }

    if (s_inflight_record_id == 0 || s_inflight_msg_id < 0) {
        return false;
    }

    int ack_id = -1;
    while (xQueueReceive(s_puback_queue, &ack_id, 0) == pdTRUE) {
        if (ack_id != s_inflight_msg_id) {
            ESP_LOGD(TAG,
                     "ignore PUBACK msg_id=%d while waiting for msg_id=%d",
                     ack_id,
                     s_inflight_msg_id);
            continue;
        }

        const uint32_t record_id = s_inflight_record_id;

        /*
         * Clear the in-flight state before touching NVS so the application can
         * continue even if NVS persistence reports an error.
         */
        s_inflight_msg_id = -1;
        s_inflight_record_id = 0;
        s_inflight_since = 0;
        s_inflight_warned = false;

        esp_err_t persist_err =
            persist_upload_mark(record_id, last_uploaded_inout);

        if (persist_error_out != NULL) {
            *persist_error_out = persist_err;
        }

        ESP_LOGI(TAG,
                 "PUBACK attendance record=%lu",
                 (unsigned long)record_id);
        return true;
    }

    TickType_t age = xTaskGetTickCount() - s_inflight_since;

    if (!s_inflight_warned &&
        age >= pdMS_TO_TICKS(CLOUD_PUBACK_WARN_MS)) {
        s_inflight_warned = true;
        ESP_LOGW(TAG,
                 "still waiting PUBACK record=%lu msg_id=%d; "
                 "not re-publishing duplicate",
                 (unsigned long)s_inflight_record_id,
                 s_inflight_msg_id);
    }

    /*
     * This is only a last-resort recovery. Normally ESP-MQTT retransmits the
     * QoS1 outbox packet itself and emits MQTT_EVENT_PUBLISHED. If no PUBACK is
     * observed for two minutes, release the application guard and allow one
     * controlled retry. The mini-program remains idempotent by record_id.
     */
    if (age >= pdMS_TO_TICKS(CLOUD_PUBACK_RETRY_MS)) {
        ESP_LOGE(TAG,
                 "PUBACK stalled for record=%lu; allowing one controlled retry",
                 (unsigned long)s_inflight_record_id);
        s_inflight_msg_id = -1;
        s_inflight_record_id = 0;
        s_inflight_since = 0;
        s_inflight_warned = false;
        xQueueReset(s_puback_queue);
        return false;
    }

    return true;
}

static esp_err_t sync_pending_records(uint32_t *last_uploaded_inout,
                                      uint32_t *pending_out)
{
    if (last_uploaded_inout == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * First consume the PUBACK for the current record. As long as one QoS1
     * record is in flight, do not call esp_mqtt_client_publish() again.
     */
    esp_err_t persist_err = ESP_OK;
    bool still_or_was_inflight =
        consume_inflight_puback(last_uploaded_inout, &persist_err);

    if (persist_err != ESP_OK) {
        /* In-memory mark was still advanced to prevent a publish storm. */
        if (pending_out != NULL) {
            *pending_out = 0;
        }
        return persist_err;
    }

    if (s_inflight_record_id != 0) {
        if (pending_out != NULL) {
            *pending_out = 1;
        }
        return ESP_OK;
    }

    /*
     * If consume_inflight_puback() completed an ACK, it is safe to continue and
     * queue the next record in this same loop. If it merely observed a stalled
     * packet and released it after the long retry window, the read below
     * deliberately returns that same record once for a controlled retry.
     */
    (void)still_or_was_inflight;

    checkin_record_t records[CLOUD_MAX_SYNC_BATCH];
    int batch_size = s_cfg.sync_batch_size;
    if (batch_size < 1 || batch_size > CLOUD_MAX_SYNC_BATCH) {
        batch_size = 8;
    }

    int count = storage_read_unuploaded_records(records,
                                                batch_size,
                                                *last_uploaded_inout);

    if (pending_out != NULL) {
        *pending_out = count > 0 ? (uint32_t)count : 0U;
    }

    if (count <= 0) {
        return ESP_OK;
    }

    char publish_topic[STORAGE_BEMFA_TOPIC_LEN + 8];
    int topic_n = snprintf(publish_topic,
                           sizeof(publish_topic),
                           "%s/set",
                           s_cfg.bemfa_topic);
    if (topic_n < 0 || (size_t)topic_n >= sizeof(publish_topic)) {
        return ESP_ERR_INVALID_SIZE;
    }

    /*
     * Queue only ONE attendance record per loop. For QoS1 the next record is
     * not queued until this one receives MQTT_EVENT_PUBLISHED/PUBACK.
     */
    const checkin_record_t *record = &records[0];

    if (record->record_id == 0 ||
        record->record_id <= *last_uploaded_inout) {
        ESP_LOGW(TAG,
                 "skip invalid/already-uploaded record=%lu mark=%lu",
                 (unsigned long)record->record_id,
                 (unsigned long)*last_uploaded_inout);
        return ESP_OK;
    }

    char payload[768];
    int payload_len =
        build_record_payload(record, payload, sizeof(payload));
    if (payload_len < 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    /*
     * Drain stale ACK ids before assigning a new application-level in-flight
     * record. There are no other application publishes in cloud_service.
     */
    xQueueReset(s_puback_queue);

    int msg_id = esp_mqtt_client_publish(s_mqtt,
                                         publish_topic,
                                         payload,
                                         payload_len,
                                         s_cfg.mqtt_qos,
                                         s_cfg.mqtt_retain ? 1 : 0);
    if (msg_id < 0) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG,
             "queued attendance record=%lu msg_id=%d qos=%d",
             (unsigned long)record->record_id,
             msg_id,
             s_cfg.mqtt_qos);

    if (s_cfg.mqtt_qos == 1) {
        s_inflight_record_id = record->record_id;
        s_inflight_msg_id = msg_id;
        s_inflight_since = xTaskGetTickCount();
        s_inflight_warned = false;
        return ESP_OK;
    }

    /*
     * QoS0 has no PUBACK event. Mark it uploaded immediately after ESP-MQTT
     * accepted the packet.
     */
    esp_err_t mark_err =
        persist_upload_mark(record->record_id, last_uploaded_inout);
    if (mark_err == ESP_OK) {
        ESP_LOGI(TAG,
                 "uploaded attendance record=%lu (QoS0)",
                 (unsigned long)record->record_id);
    }
    return mark_err;
}

static void cloud_task(void *arg)
{
    (void)arg;
    uint32_t last_uploaded = 0;
    (void)storage_get_upload_mark(&last_uploaded);
    ESP_LOGI(TAG, "attendance upload mark=%lu", (unsigned long)last_uploaded);

    bool warned_invalid_cloud_cfg = false;
    bool wifi_started = false;
    bool wifi_start_attempted = false;

    /*
     * Let camera init and deliver frames before the radio stack is brought up.
     * ESP32-S3 camera/Wi-Fi coexistence is sensitive during GDMA startup.
     */
    vTaskDelay(pdMS_TO_TICKS(CLOUD_NETWORK_START_DELAY_MS));

    while (1) {
        esp_err_t last_error = ESP_OK;
        uint32_t pending = 0;

        /*
         * Do not turn on Wi-Fi when cloud sync is disabled. This preserves the
         * original fully-offline camera/attendance behavior by default.
         */
        if (s_cfg.cloud_enabled && !wifi_started && !wifi_start_attempted) {
            wifi_start_attempted = true;
            esp_err_t wifi_err = start_wifi_service();
            if (wifi_err == ESP_OK) {
                wifi_started = true;
                ESP_LOGI(TAG, "Wi-Fi provisioning/network service started");
            } else {
                /*
                 * The existing provisioning component allocates its netifs and
                 * mutex before esp_wifi_init(), but exposes no rollback API.
                 * Repeating a hard initialization failure in the same boot can
                 * consume more internal RAM, so fail once and wait for reboot.
                 */
                last_error = wifi_err;
                ESP_LOGE(TAG,
                         "Wi-Fi start failed: %s; not retrying in this boot",
                         esp_err_to_name(wifi_err));
            }
        }

        wifi_provisioning_status_t wifi_status = {0};
        bool wifi_connected = wifi_started &&
                              wifi_provisioning_get_status(&wifi_status) == ESP_OK &&
                              wifi_status.state == WIFI_PROV_STATE_CONNECTED;
        bool mqtt_connected = (xEventGroupGetBits(s_event_group) & CLOUD_MQTT_CONNECTED_BIT) != 0;

        if (wifi_connected) {
            maintain_sntp();

            if (s_cfg.cloud_enabled) {
                if (!cloud_credentials_valid()) {
                    if (!warned_invalid_cloud_cfg) {
                        ESP_LOGW(TAG, "BaFa Cloud enabled but uid/topic/broker config is invalid");
                        warned_invalid_cloud_cfg = true;
                    }
                    last_error = ESP_ERR_INVALID_ARG;
                } else {
                    warned_invalid_cloud_cfg = false;
                    esp_err_t mqtt_err = start_mqtt();
                    if (mqtt_err != ESP_OK && mqtt_err != ESP_ERR_INVALID_STATE) {
                        last_error = mqtt_err;
                    }
                }
            }
        }

        mqtt_connected = (xEventGroupGetBits(s_event_group) & CLOUD_MQTT_CONNECTED_BIT) != 0;
        if (wifi_connected && mqtt_connected && s_cfg.cloud_enabled) {
            esp_err_t sync_err = sync_pending_records(&last_uploaded, &pending);
            if (sync_err != ESP_OK && sync_err != ESP_ERR_INVALID_STATE) {
                last_error = sync_err;
            }
        }

        status_update(wifi_connected,
                      mqtt_connected,
                      system_time_valid(),
                      last_uploaded,
                      pending,
                      last_error);
        vTaskDelay(pdMS_TO_TICKS(CLOUD_POLL_MS));
    }
}

esp_err_t cloud_service_start(void)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t cfg_err = storage_attendance_config_load(&s_cfg);
    if (cfg_err != ESP_OK) {
        ESP_LOGW(TAG, "attendance config load returned %s; continuing with parsed/default values",
                 esp_err_to_name(cfg_err));
    }

    if (s_cfg.timezone[0] != '\0') {
        setenv("TZ", s_cfg.timezone, 1);
        tzset();
    }

    s_event_group = xEventGroupCreate();
    s_puback_queue = xQueueCreate(8, sizeof(int));
    s_status_lock = xSemaphoreCreateMutex();
    if (s_event_group == NULL || s_puback_queue == NULL || s_status_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memset(&s_status, 0, sizeof(s_status));
    s_status.cloud_enabled = s_cfg.cloud_enabled;

    if (xTaskCreate(cloud_task,
                    "cloud_sync",
                    7168,
                    NULL,
                    2,
                    NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    ESP_LOGI(TAG, "cloud/time service started (cloud_enabled=%d)", s_cfg.cloud_enabled);
    return ESP_OK;
}

esp_err_t cloud_service_get_status(cloud_service_status_t *out)
{
    if (!s_started || out == NULL || s_status_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_status_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    *out = s_status;
    xSemaphoreGive(s_status_lock);
    return ESP_OK;
}

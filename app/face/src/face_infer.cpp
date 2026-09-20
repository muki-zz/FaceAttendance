#include <cstdint>
#include <cstdio>
#include <list>
#include <new>
#include <time.h>

#include "human_face_detect.hpp"
#include "human_face_recognition.hpp"
#include "dl_image_define.hpp"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "face_common.h"
#include "cloud_service.h"
#include "frame.h"
#include "storage.h"
#include "timetable.h"

static const char *TAG = "face_engine";

static QueueHandle_t s_frame_queue = nullptr;
static QueueHandle_t s_command_queue = nullptr;
static QueueHandle_t s_event_queue = nullptr;
static TaskHandle_t s_face_task = nullptr;

static HumanFaceDetect *s_detector = nullptr;
static HumanFaceRecognizer *s_recognizer = nullptr;

typedef struct {
    int face_id;
    uint32_t session_id;
    int64_t last_saved_us;
    int64_t record_timestamp;
    checkin_status_t status;
} recent_face_t;

static recent_face_t s_recent_faces[16] = {};
static attendance_config_t s_attendance_cfg = {};
static constexpr int64_t TIME_SYNC_GRACE_US = 45LL * 1000000LL;

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == nullptr || dst_size == 0) {
        return;
    }
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

static void post_event(const face_event_t &event)
{
    if (s_event_queue == nullptr) {
        return;
    }

    if (xQueueSend(s_event_queue, &event, pdMS_TO_TICKS(20)) == pdPASS) {
        return;
    }

    // Recognition events are high frequency. Drop the oldest event rather than blocking the model task.
    face_event_t stale = {};
    xQueueReceive(s_event_queue, &stale, 0);
    xQueueSend(s_event_queue, &event, 0);
}

static bool wall_clock_valid(int64_t timestamp)
{
    return timestamp >= STORAGE_TIME_VALID_EPOCH;
}

static int local_minute_of_day(int64_t timestamp)
{
    time_t value = (time_t)timestamp;
    struct tm local = {};
    localtime_r(&value, &local);
    return local.tm_hour * 60 + local.tm_min;
}

static bool cooldown_active(int face_id,
                            uint32_t session_id,
                            uint16_t cooldown_sec,
                            int64_t *record_timestamp,
                            checkin_status_t *status)
{
    if (cooldown_sec == 0) {
        return false;
    }

    const int64_t now = esp_timer_get_time();
    const int64_t cooldown_us = (int64_t)cooldown_sec * 1000000LL;

    for (auto &entry : s_recent_faces) {
        if (entry.face_id == face_id &&
            entry.session_id == session_id &&
            entry.last_saved_us > 0 &&
            now - entry.last_saved_us < cooldown_us) {
            if (record_timestamp != nullptr) {
                *record_timestamp = entry.record_timestamp;
            }
            if (status != nullptr) {
                *status = entry.status;
            }
            return true;
        }
    }
    return false;
}

static void remember_checkin(int face_id,
                            uint32_t session_id,
                            int64_t timestamp,
                            checkin_status_t status)
{
    int slot = -1;
    int64_t oldest = INT64_MAX;
    int64_t now = esp_timer_get_time();

    for (int i = 0; i < (int)(sizeof(s_recent_faces) / sizeof(s_recent_faces[0])); ++i) {
        if (s_recent_faces[i].face_id == face_id &&
            s_recent_faces[i].session_id == session_id) {
            s_recent_faces[i].last_saved_us = now;
            s_recent_faces[i].record_timestamp = timestamp;
            s_recent_faces[i].status = status;
            return;
        }
        if (s_recent_faces[i].face_id == 0) {
            slot = i;
            break;
        }
        if (s_recent_faces[i].last_saved_us < oldest) {
            oldest = s_recent_faces[i].last_saved_us;
            slot = i;
        }
    }

    if (slot >= 0) {
        s_recent_faces[slot].face_id = face_id;
        s_recent_faces[slot].session_id = session_id;
        s_recent_faces[slot].last_saved_us = now;
        s_recent_faces[slot].record_timestamp = timestamp;
        s_recent_faces[slot].status = status;
    }
}

static void emit_enroll_failure(const face_enroll_request_t &request,
                                face_enroll_fail_reason_t reason,
                                esp_err_t err)
{
    face_event_t event = {};
    event.type = FACE_EVENT_ENROLL_FAILED;
    event.err = err;
    event.enroll_fail_reason = reason;
    event.class_id = request.class_id;
    copy_string(event.student_id, sizeof(event.student_id), request.student_id);
    copy_string(event.name, sizeof(event.name), request.name);
    post_event(event);
}

static void process_enroll(const face_enroll_request_t &request,
                           const dl::image::img_t &img,
                           const std::list<dl::detect::result_t> &detect_results)
{
    student_profile_t existing = {};
    if (storage_student_find_by_student_id(request.student_id, &existing) == ESP_OK) {
        emit_enroll_failure(request, FACE_ENROLL_FAIL_DUPLICATE_STUDENT, ESP_ERR_INVALID_STATE);
        return;
    }

    face_event_t started = {};
    started.type = FACE_EVENT_ENROLL_STARTED;
    started.class_id = request.class_id;
    copy_string(started.student_id, sizeof(started.student_id), request.student_id);
    copy_string(started.name, sizeof(started.name), request.name);
    post_event(started);

    if (detect_results.empty()) {
        emit_enroll_failure(request, FACE_ENROLL_FAIL_NO_FACE, ESP_ERR_NOT_FOUND);
        return;
    }
    if (detect_results.size() != 1) {
        emit_enroll_failure(request, FACE_ENROLL_FAIL_MULTI_FACE, ESP_ERR_INVALID_STATE);
        return;
    }

    // Avoid mapping the same already-enrolled face to another student ID.
    auto duplicate = s_recognizer->recognize(img, detect_results);
    if (!duplicate.empty()) {
        emit_enroll_failure(request, FACE_ENROLL_FAIL_DUPLICATE_FACE, ESP_ERR_INVALID_STATE);
        return;
    }

    esp_err_t err = s_recognizer->enroll(img, detect_results);
    if (err != ESP_OK) {
        emit_enroll_failure(request, FACE_ENROLL_FAIL_MODEL, err);
        return;
    }

    // ESP-DL database allocates features in enrollment order. We do not expose delete in this phase,
    // so get_num_feats() is also the new recognition id (1-based).
    int face_id = s_recognizer->get_num_feats();

    student_profile_t student = {};
    student.face_id = face_id;
    student.class_id = request.class_id;
    student.created_ts = (int64_t)time(nullptr);
    copy_string(student.student_id, sizeof(student.student_id), request.student_id);
    copy_string(student.name, sizeof(student.name), request.name);

    err = storage_student_add(&student);
    if (err != ESP_OK) {
        // Keep face DB and student mapping atomic from the application's point of view.
        s_recognizer->delete_last_feat();
        emit_enroll_failure(request, FACE_ENROLL_FAIL_STORAGE, err);
        return;
    }

    face_event_t event = {};
    event.type = FACE_EVENT_ENROLL_SUCCESS;
    event.face_id = face_id;
    event.class_id = request.class_id;
    copy_string(event.student_id, sizeof(event.student_id), request.student_id);
    copy_string(event.name, sizeof(event.name), request.name);
    post_event(event);

    ESP_LOGI(TAG, "enrolled face=%d student=%s name=%s", face_id, request.student_id, request.name);
}

static void process_recognition(const dl::image::img_t &img,
                                const std::list<dl::detect::result_t> &detect_results)
{
    if (detect_results.empty()) {
        return;
    }

    auto results = s_recognizer->recognize(img, detect_results);
    if (results.empty()) {
        face_event_t unknown = {};
        unknown.type = FACE_EVENT_UNKNOWN;
        post_event(unknown);
        return;
    }

    const auto &best = results[0];
    student_profile_t student = {};
    esp_err_t err = storage_student_find_by_face_id(best.id, &student);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "matched face id=%d has no student mapping", best.id);
        face_event_t unknown = {};
        unknown.type = FACE_EVENT_UNKNOWN;
        unknown.face_id = best.id;
        unknown.similarity = best.similarity;
        unknown.err = err;
        post_event(unknown);
        return;
    }

    face_event_t event = {};
    event.type = FACE_EVENT_RECOGNIZED;
    event.face_id = best.id;
    event.similarity = best.similarity;
    event.class_id = student.class_id;
    event.checkin_status = CHECKIN_STATUS_NORMAL;
    event.checkin_result = FACE_CHECKIN_RESULT_NONE;
    event.checkin_timestamp = (int64_t)time(nullptr);
    event.checkin_time_valid = wall_clock_valid(event.checkin_timestamp);
    copy_string(event.student_id, sizeof(event.student_id), student.student_id);
    copy_string(event.name, sizeof(event.name), student.name);

    /*
     * A timetable-based attendance decision requires a valid local date/time.
     * If Wi-Fi is already connected, preserve the existing short SNTP grace
     * period; after that we still refuse to invent a course/session offline.
     */
    if (!event.checkin_time_valid) {
        if (s_attendance_cfg.cloud_enabled) {
            cloud_service_status_t cloud_status = {};
            if (cloud_service_get_status(&cloud_status) == ESP_OK &&
                cloud_status.wifi_connected &&
                !cloud_status.time_synced &&
                esp_timer_get_time() < TIME_SYNC_GRACE_US) {
                event.checkin_result = FACE_CHECKIN_RESULT_TIME_SYNCING;
                post_event(event);
                return;
            }
        }

        event.checkin_result = FACE_CHECKIN_RESULT_TIME_REQUIRED;
        post_event(event);
        return;
    }

    const time_t now = (time_t)event.checkin_timestamp;
    if (!timetable_matches_date(now)) {
        event.checkin_result = FACE_CHECKIN_RESULT_NO_SCHEDULE;
        post_event(event);
        return;
    }

    timetable_entry_t session = {};
    err = timetable_get_current_session(now, &session);
    if (err != ESP_OK) {
        event.checkin_result = FACE_CHECKIN_RESULT_NO_ACTIVE_SESSION;
        post_event(event);
        return;
    }

    event.session_id = session.session_id;
    event.course_id = session.course_id;
    event.period = session.period;
    copy_string(event.course_name, sizeof(event.course_name), session.course_name);

    /* class_id == 0 means the session accepts any registered class. */
    if (session.class_id != 0 && student.class_id != session.class_id) {
        event.checkin_result = FACE_CHECKIN_RESULT_WRONG_CLASS;
        post_event(event);
        return;
    }

    int64_t recent_timestamp = 0;
    checkin_status_t recent_status = CHECKIN_STATUS_NORMAL;
    if (cooldown_active(best.id,
                        session.session_id,
                        s_attendance_cfg.cooldown_sec,
                        &recent_timestamp,
                        &recent_status)) {
        event.checkin_result = FACE_CHECKIN_RESULT_DUPLICATE;
        if (recent_timestamp != 0) {
            event.checkin_timestamp = recent_timestamp;
            event.checkin_status = recent_status;
        }
        post_event(event);
        return;
    }

    checkin_record_t previous = {};
    if (storage_get_checkin_for_student_session(student.student_id,
                                                session.session_id,
                                                &previous) == ESP_OK) {
        event.checkin_result = FACE_CHECKIN_RESULT_DUPLICATE;
        event.checkin_timestamp = previous.timestamp;
        event.checkin_time_valid = wall_clock_valid(previous.timestamp);
        event.checkin_status = previous.status;
        post_event(event);
        return;
    }

    int minute = local_minute_of_day(event.checkin_timestamp);
    event.checkin_status = minute >= session.late_minute
                               ? CHECKIN_STATUS_LATE
                               : CHECKIN_STATUS_NORMAL;

    checkin_record_t record = {};
    record.face_id = best.id;
    record.class_id = student.class_id;
    record.timestamp = event.checkin_timestamp;
    record.status = event.checkin_status;
    record.session_id = session.session_id;
    record.course_id = session.course_id;
    record.period = session.period;
    copy_string(record.student_id, sizeof(record.student_id), student.student_id);
    copy_string(record.name, sizeof(record.name), student.name);
    copy_string(record.course_name, sizeof(record.course_name), session.course_name);

    err = storage_checkin_add(&record);
    if (err == ESP_OK) {
        event.checkin_saved = true;
        event.checkin_result = FACE_CHECKIN_RESULT_SAVED;
        remember_checkin(best.id,
                         session.session_id,
                         record.timestamp,
                         record.status);
        ESP_LOGI(TAG,
                 "check-in saved student=%s session=%lu course=%u period=%u status=%s",
                 student.student_id,
                 (unsigned long)session.session_id,
                 (unsigned)session.course_id,
                 (unsigned)session.period,
                 storage_checkin_status_str(record.status));
    } else {
        event.err = err;
        event.checkin_result = FACE_CHECKIN_RESULT_STORAGE_ERROR;
        ESP_LOGE(TAG, "save checkin failed: %s", esp_err_to_name(err));
    }

    post_event(event);
}

static void face_task(void *arg)
{
    (void)arg;

    esp_err_t cfg_err = storage_attendance_config_load(&s_attendance_cfg);
    if (cfg_err != ESP_OK) {
        ESP_LOGW(TAG, "attendance config load returned %s; continuing with parsed/default values",
                 esp_err_to_name(cfg_err));
    }
    ESP_LOGI(TAG,
             "timetable attendance enabled; cooldown=%us, max daily sessions=%u",
             (unsigned)s_attendance_cfg.cooldown_sec,
             (unsigned)TIMETABLE_MAX_SESSIONS);

    s_detector = new (std::nothrow) HumanFaceDetect();
    s_recognizer = new (std::nothrow) HumanFaceRecognizer(FACE_DB_PATH);
    if (s_detector == nullptr || s_recognizer == nullptr) {
        face_event_t event = {};
        event.type = FACE_EVENT_ERROR;
        event.err = ESP_ERR_NO_MEM;
        post_event(event);
        delete s_detector;
        delete s_recognizer;
        s_detector = nullptr;
        s_recognizer = nullptr;
        s_face_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    face_event_t ready = {};
    ready.type = FACE_EVENT_MODEL_READY;
    post_event(ready);
    ESP_LOGI(TAG, "model ready, feature count=%d", s_recognizer->get_num_feats());

    face_command_t pending_command = {};
    bool has_pending_command = false;

    while (1) {
        camera_frame_t frame = {};
        if (xQueueReceive(s_frame_queue, &frame, portMAX_DELAY) != pdPASS) {
            continue;
        }

        if (frame.buf == nullptr || frame.w == 0 || frame.h == 0 ||
            frame.len < (uint32_t)frame.w * frame.h * 2U) {
            camera_frame_release(&frame);
            continue;
        }

        if (!has_pending_command &&
            xQueueReceive(s_command_queue, &pending_command, 0) == pdPASS) {
            has_pending_command = true;
        }

        dl::image::img_t img = {};
        img.data = frame.buf;
        img.width = frame.w;
        img.height = frame.h;
        img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565BE;

        std::list<dl::detect::result_t> detect_results = s_detector->run(img);

        if (has_pending_command && pending_command.type == FACE_CMD_ENROLL) {
            if (frame.captured_us >= pending_command.request_us) {
                process_enroll(pending_command.enroll, img, detect_results);
                has_pending_command = false;
                pending_command = {};
            } else {
                // The queued frame predates the button press. Never enroll a stale face.
                process_recognition(img, detect_results);
            }
        } else {
            process_recognition(img, detect_results);
        }

        camera_frame_release(&frame);
    }
}

extern "C" esp_err_t face_engine_start(QueueHandle_t frame_queue,
                                        QueueHandle_t command_queue,
                                        QueueHandle_t event_queue)
{
    if (frame_queue == nullptr || command_queue == nullptr || event_queue == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_face_task != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    s_frame_queue = frame_queue;
    s_command_queue = command_queue;
    s_event_queue = event_queue;

    BaseType_t ok = xTaskCreatePinnedToCore(face_task,
                                            "face_engine",
                                            14336,
                                            nullptr,
                                            6,
                                            &s_face_task,
                                            1);
    if (ok != pdPASS) {
        s_face_task = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

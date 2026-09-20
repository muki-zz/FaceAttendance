#include "gui_common.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cloud_service.h"
#include "lvgl.h"
#include "storage.h"

static lv_obj_t *s_home_page = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_person_label = NULL;
static lv_obj_t *s_clock_label = NULL;
static lv_obj_t *s_cloud_label = NULL;
static lv_timer_t *s_clock_timer = NULL;
static attendance_config_t s_attendance_cfg;

#define HOME_NAV_X       270
#define HOME_NAV_W       195
#define HOME_NAV_H       42
#define HOME_NAV_Y0      55
#define HOME_NAV_STEP    49

static lv_obj_t *make_nav_button(lv_obj_t *parent,
                                 const char *text,
                                 int slot,
                                 gui_page_id_t target,
                                 bool primary)
{
    lv_obj_t *button =
        gui_create_button(parent,
                          text,
                          HOME_NAV_W,
                          HOME_NAV_H,
                          primary);

    lv_obj_set_pos(button,
                   HOME_NAV_X,
                   HOME_NAV_Y0 +
                       slot * HOME_NAV_STEP);

    lv_obj_add_event_cb(button,
                        gui_nav_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(intptr_t)target);

    return button;
}

static void format_event_time(const face_event_t *event,
                              char *buf,
                              size_t size)
{
    if (event == NULL || buf == NULL || size == 0) {
        return;
    }

    if (!event->checkin_time_valid ||
        event->checkin_timestamp < STORAGE_TIME_VALID_EPOCH) {
        snprintf(buf, size, "%s", "--:--:--");
        return;
    }

    time_t value = (time_t)event->checkin_timestamp;
    struct tm local = {0};
    localtime_r(&value, &local);
    strftime(buf, size, "%H:%M:%S", &local);
}

static const char *short_status(checkin_status_t status)
{
    switch (status) {
        case CHECKIN_STATUS_LATE:
            return "LATE";
        case CHECKIN_STATUS_ABSENT:
            return "ABSENT";
        case CHECKIN_STATUS_TIME_UNSYNCED:
            return "UNSYNC";
        case CHECKIN_STATUS_NORMAL:
        default:
            return "NORMAL";
    }
}

static void update_clock_and_cloud(void)
{
    if (s_clock_label == NULL || s_cloud_label == NULL) {
        return;
    }

    time_t now = time(NULL);

    if ((int64_t)now >= STORAGE_TIME_VALID_EPOCH) {
        struct tm local = {0};
        char text[32] = {0};
        localtime_r(&now, &local);
        strftime(text, sizeof(text), "%Y-%m-%d  %H:%M:%S", &local);

        lv_label_set_text(s_clock_label, text);
        lv_obj_set_style_text_color(s_clock_label,
                                    gui_color_primary(),
                                    0);
    } else {
        lv_label_set_text(s_clock_label, "Time syncing...");
        lv_obj_set_style_text_color(s_clock_label,
                                    gui_color_warning(),
                                    0);
    }

    cloud_service_status_t status = {0};
    if (cloud_service_get_status(&status) == ESP_OK) {
        char cloud_text[72];
        snprintf(cloud_text,
                 sizeof(cloud_text),
                 "WiFi:%s  MQTT:%s  TIME:%s",
                 status.wifi_connected ? "ON" : "--",
                 status.mqtt_connected ? "ON" : "--",
                 status.time_synced ? "OK" : "--");

        lv_label_set_text(s_cloud_label, cloud_text);
        lv_obj_set_style_text_color(
            s_cloud_label,
            status.time_synced ? gui_color_success()
                               : gui_color_muted(),
            0);
    } else {
        lv_label_set_text(s_cloud_label, "WiFi:--  MQTT:--  TIME:--");
        lv_obj_set_style_text_color(s_cloud_label,
                                    gui_color_muted(),
                                    0);
    }
}

static void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_clock_and_cloud();
}

lv_obj_t *create_home_page(void)
{
    s_home_page = lv_obj_create(NULL);
    gui_style_screen(s_home_page);
    lv_obj_set_style_pad_all(s_home_page, 0, 0);

    lv_obj_t *title = gui_create_title(
        s_home_page,
        "FaceAttend  Campus Check-in");
    lv_obj_set_pos(title, 12, 10);

    s_clock_label = lv_label_create(s_home_page);
    lv_obj_set_width(s_clock_label, 215);
    lv_obj_set_pos(s_clock_label, 252, 10);
    lv_obj_set_style_text_align(s_clock_label,
                                LV_TEXT_ALIGN_RIGHT,
                                0);
    lv_obj_set_style_text_color(s_clock_label,
                                gui_color_primary(),
                                0);

    s_cloud_label = lv_label_create(s_home_page);
    lv_obj_set_width(s_cloud_label, 215);
    lv_obj_set_pos(s_cloud_label, 252, 31);
    lv_obj_set_style_text_align(s_cloud_label,
                                LV_TEXT_ALIGN_RIGHT,
                                0);
    lv_obj_set_style_text_color(s_cloud_label,
                                gui_color_muted(),
                                0);

    /*
     * Camera preview is drawn directly by camera_preview.c at the fixed
     * FACE_PREVIEW_* coordinates, so the decorative frame stays aligned
     * with those coordinates.
     */
    lv_obj_t *preview_border = lv_obj_create(s_home_page);
    lv_obj_set_pos(preview_border,
                   FACE_PREVIEW_X - 3,
                   FACE_PREVIEW_Y - 3);
    lv_obj_set_size(preview_border,
                    FACE_PREVIEW_W + 6,
                    FACE_PREVIEW_H + 6);
    lv_obj_set_style_bg_opa(preview_border, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(preview_border, 2, 0);
    lv_obj_set_style_border_color(preview_border,
                                  gui_color_primary(),
                                  0);
    lv_obj_set_style_radius(preview_border, 8, 0);
    lv_obj_clear_flag(preview_border, LV_OBJ_FLAG_SCROLLABLE);

    make_nav_button(s_home_page,
                    "Register Face",
                    0,
                    GUI_PAGE_REGISTER,
                    true);

    make_nav_button(s_home_page,
                    "Check-in Records",
                    1,
                    GUI_PAGE_RECORD,
                    true);

    make_nav_button(s_home_page,
                    "Attendance Stats",
                    2,
                    GUI_PAGE_STAT,
                    true);

    make_nav_button(s_home_page,
                    "Today Schedule",
                    3,
                    GUI_PAGE_SCHEDULE,
                    false);

    s_person_label = lv_label_create(s_home_page);
    lv_label_set_text(s_person_label, "Waiting for face...");
    lv_obj_set_style_text_color(s_person_label,
                                gui_color_text(),
                                0);
    lv_obj_set_pos(s_person_label, 12, 244);
    lv_obj_set_width(s_person_label, 240);

    lv_obj_t *status_card = lv_obj_create(s_home_page);
    lv_obj_set_pos(status_card, HOME_NAV_X, 252);
    lv_obj_set_size(status_card, HOME_NAV_W, 56);
    lv_obj_set_style_bg_color(status_card,
                              gui_color_surface(),
                              0);
    lv_obj_set_style_bg_opa(status_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(status_card,
                                  lv_color_hex(GUI_COLOR_BORDER_HEX),
                                  0);
    lv_obj_set_style_border_width(status_card, 1, 0);
    lv_obj_set_style_radius(status_card, 9, 0);
    lv_obj_set_style_pad_all(status_card, 5, 0);
    lv_obj_clear_flag(status_card, LV_OBJ_FLAG_SCROLLABLE);

    s_status_label = lv_label_create(status_card);
    lv_label_set_text(s_status_label, "Face model starting...");
    lv_obj_set_style_text_color(s_status_label,
                                gui_color_muted(),
                                0);
    lv_obj_set_width(s_status_label, 177);
    lv_obj_align(s_status_label, LV_ALIGN_TOP_LEFT, 0, 0);

    memset(&s_attendance_cfg, 0, sizeof(s_attendance_cfg));
    (void)storage_attendance_config_load(&s_attendance_cfg);

    update_clock_and_cloud();
    if (s_clock_timer == NULL) {
        s_clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
    }

    return s_home_page;
}

lv_obj_t *get_home_page(void)
{
    return s_home_page;
}

void gui_home_handle_face_event(const face_event_t *event)
{
    if (event == NULL ||
        s_status_label == NULL ||
        s_person_label == NULL) {
        return;
    }

    char text[180];
    char time_text[24];

    switch (event->type) {
        case FACE_EVENT_MODEL_READY:
            lv_label_set_text(s_status_label, "Face model ready");
            lv_obj_set_style_text_color(s_status_label,
                                        gui_color_success(),
                                        0);
            break;

        case FACE_EVENT_RECOGNIZED:
            format_event_time(event,
                              time_text,
                              sizeof(time_text));

            snprintf(text,
                     sizeof(text),
                     "%s  %s\nSimilarity %.3f",
                     event->student_id,
                     event->name,
                     event->similarity);
            lv_label_set_text(s_person_label, text);

            switch (event->checkin_result) {
                case FACE_CHECKIN_RESULT_SAVED:
                    snprintf(text,
                             sizeof(text),
                             "%s  %s\nCheck-in saved",
                             short_status(event->checkin_status),
                             time_text);
                    lv_obj_set_style_text_color(
                        s_status_label,
                        event->checkin_status == CHECKIN_STATUS_LATE
                            ? gui_color_warning()
                            : gui_color_success(),
                        0);
                    break;

                case FACE_CHECKIN_RESULT_TIME_UNSYNCED_SAVED:
                    snprintf(text,
                             sizeof(text),
                             "TIME UNSYNCED\nSaved offline");
                    lv_obj_set_style_text_color(s_status_label,
                                                gui_color_warning(),
                                                0);
                    break;

                case FACE_CHECKIN_RESULT_TIME_SYNCING:
                    snprintf(text,
                             sizeof(text),
                             "Time syncing...\nPlease try again");
                    lv_obj_set_style_text_color(s_status_label,
                                                gui_color_warning(),
                                                0);
                    break;

                case FACE_CHECKIN_RESULT_DUPLICATE:
                    snprintf(text,
                             sizeof(text),
                             "Already checked in\n%s",
                             time_text);
                    lv_obj_set_style_text_color(s_status_label,
                                                gui_color_muted(),
                                                0);
                    break;

                case FACE_CHECKIN_RESULT_BEFORE_WINDOW:
                    snprintf(text,
                             sizeof(text),
                             "Too early\nOpens %02u:%02u",
                             (unsigned)(s_attendance_cfg.start_minute / 60U),
                             (unsigned)(s_attendance_cfg.start_minute % 60U));
                    lv_obj_set_style_text_color(s_status_label,
                                                gui_color_warning(),
                                                0);
                    break;

                case FACE_CHECKIN_RESULT_AFTER_WINDOW:
                    snprintf(text,
                             sizeof(text),
                             "Window closed\nEnded %02u:%02u",
                             (unsigned)(s_attendance_cfg.end_minute / 60U),
                             (unsigned)(s_attendance_cfg.end_minute % 60U));
                    lv_obj_set_style_text_color(s_status_label,
                                                gui_color_warning(),
                                                0);
                    break;

                case FACE_CHECKIN_RESULT_TIME_REQUIRED:
                    snprintf(text,
                             sizeof(text),
                             "Time unavailable\nSchedule check blocked");
                    lv_obj_set_style_text_color(s_status_label,
                                                gui_color_warning(),
                                                0);
                    break;

                case FACE_CHECKIN_RESULT_NO_SCHEDULE:
                    snprintf(text,
                             sizeof(text),
                             "No semester schedule\nSync schedule");
                    lv_obj_set_style_text_color(s_status_label,
                                                gui_color_warning(),
                                                0);
                    break;

                case FACE_CHECKIN_RESULT_NO_ACTIVE_SESSION:
                    snprintf(text,
                             sizeof(text),
                             "No class now\nCheck today's schedule");
                    lv_obj_set_style_text_color(s_status_label,
                                                gui_color_muted(),
                                                0);
                    break;

                case FACE_CHECKIN_RESULT_WRONG_CLASS:
                    snprintf(text,
                             sizeof(text),
                             "Wrong class\n%s P%u",
                             event->course_name[0] != '\0'
                                 ? event->course_name
                                 : "Current course",
                             (unsigned)event->period);
                    lv_obj_set_style_text_color(s_status_label,
                                                gui_color_warning(),
                                                0);
                    break;

                case FACE_CHECKIN_RESULT_STORAGE_ERROR:
                    snprintf(text,
                             sizeof(text),
                             "Check-in failed\nStorage error");
                    lv_obj_set_style_text_color(s_status_label,
                                                gui_color_warning(),
                                                0);
                    break;

                default:
                    snprintf(text,
                             sizeof(text),
                             "Recognized\n%s",
                             time_text);
                    lv_obj_set_style_text_color(s_status_label,
                                                gui_color_text(),
                                                0);
                    break;
            }

            lv_label_set_text(s_status_label, text);
            break;

        case FACE_EVENT_UNKNOWN:
            lv_label_set_text(s_person_label, "Unknown face");
            lv_label_set_text(s_status_label, "Not enrolled");
            lv_obj_set_style_text_color(s_status_label,
                                        gui_color_warning(),
                                        0);
            break;

        case FACE_EVENT_ERROR:
            lv_label_set_text(s_status_label, "Face service error");
            lv_obj_set_style_text_color(s_status_label,
                                        gui_color_warning(),
                                        0);
            break;

        default:
            break;
    }
}

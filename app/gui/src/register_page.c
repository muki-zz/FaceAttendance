#include "gui_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "face_service.h"
#include "lvgl.h"

static const char *TAG = "register_page";

static lv_obj_t *s_page = NULL;
static lv_obj_t *s_student_id = NULL;
static lv_obj_t *s_name = NULL;
static lv_obj_t *s_class_id = NULL;
static lv_obj_t *s_status = NULL;
static lv_obj_t *s_enroll_btn = NULL;
static lv_obj_t *s_keyboard = NULL;

static void keyboard_focus_cb(lv_event_t *event)
{
    lv_obj_t *ta = lv_event_get_target(event);
    face_service_set_preview_enabled(false);
    lv_keyboard_set_textarea(s_keyboard, ta);
    lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_keyboard);
}

static void keyboard_ready_cb(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    face_service_set_preview_enabled(true);
}

static void enroll_click_cb(lv_event_t *event)
{
    (void)event;

    const char *student_id = lv_textarea_get_text(s_student_id);
    const char *name = lv_textarea_get_text(s_name);
    const char *class_text = lv_textarea_get_text(s_class_id);

    if (student_id == NULL ||
        name == NULL ||
        student_id[0] == '\0' ||
        name[0] == '\0') {
        lv_label_set_text(s_status,
                          "Student ID and name are required");
        lv_obj_set_style_text_color(s_status,
                                    gui_color_warning(),
                                    0);
        return;
    }

    face_enroll_request_t request = {0};
    snprintf(request.student_id,
             sizeof(request.student_id),
             "%s",
             student_id);
    snprintf(request.name,
             sizeof(request.name),
             "%s",
             name);
    request.class_id =
        (class_text != NULL && class_text[0] != '\0')
            ? atoi(class_text)
            : 0;

    esp_err_t err = face_service_request_enroll(&request);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "enroll request failed: %s",
                 esp_err_to_name(err));
        lv_label_set_text(s_status,
                          "Face service busy, try again");
        lv_obj_set_style_text_color(s_status,
                                    gui_color_warning(),
                                    0);
        return;
    }

    lv_obj_add_state(s_enroll_btn, LV_STATE_DISABLED);
    lv_label_set_text(s_status,
                      "Waiting for a clear single face...");
    lv_obj_set_style_text_color(s_status,
                                gui_color_muted(),
                                0);
}

static void back_click_cb(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    gui_show_page(GUI_PAGE_HOME);
}

static lv_obj_t *create_field(lv_obj_t *parent,
                              const char *placeholder,
                              int y,
                              bool numeric)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_pos(ta, 270, y);
    lv_obj_set_size(ta, 195, 42);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    gui_style_input(ta);

    if (numeric) {
        lv_textarea_set_accepted_chars(ta, "0123456789");
    }

    lv_obj_add_event_cb(ta,
                        keyboard_focus_cb,
                        LV_EVENT_FOCUSED,
                        NULL);
    return ta;
}

lv_obj_t *create_register_page(void)
{
    s_page = lv_obj_create(NULL);
    gui_style_screen(s_page);
    lv_obj_set_style_pad_all(s_page, 0, 0);

    lv_obj_t *title =
        gui_create_title(s_page, "Register Student Face");
    lv_obj_set_pos(title, 12, 10);

    lv_obj_t *subtitle = lv_label_create(s_page);
    lv_label_set_text(subtitle,
                      "Keep one face centered in the camera");
    lv_obj_set_style_text_color(subtitle,
                                gui_color_muted(),
                                0);
    lv_obj_set_pos(subtitle, 270, 24);

    lv_obj_t *preview_border = lv_obj_create(s_page);
    lv_obj_set_pos(preview_border,
                   FACE_PREVIEW_X - 3,
                   FACE_PREVIEW_Y - 3);
    lv_obj_set_size(preview_border,
                    FACE_PREVIEW_W + 6,
                    FACE_PREVIEW_H + 6);
    lv_obj_set_style_bg_opa(preview_border,
                            LV_OPA_TRANSP,
                            0);
    lv_obj_set_style_border_width(preview_border, 2, 0);
    lv_obj_set_style_border_color(preview_border,
                                  gui_color_primary(),
                                  0);
    lv_obj_set_style_radius(preview_border, 8, 0);
    lv_obj_clear_flag(preview_border,
                      LV_OBJ_FLAG_SCROLLABLE);

    s_student_id =
        create_field(s_page, "Student ID", 52, false);
    lv_textarea_set_max_length(s_student_id,
                               STORAGE_STUDENT_ID_LEN - 1);

    s_name =
        create_field(s_page, "Name", 101, false);
    lv_textarea_set_max_length(s_name,
                               STORAGE_STUDENT_NAME_LEN - 1);

    s_class_id =
        create_field(s_page, "Class ID", 150, true);
    lv_textarea_set_max_length(s_class_id, 9);

    s_enroll_btn =
        gui_create_button(s_page,
                          "Start Enrollment",
                          195,
                          42,
                          true);
    lv_obj_set_pos(s_enroll_btn, 270, 201);
    lv_obj_add_event_cb(s_enroll_btn,
                        enroll_click_cb,
                        LV_EVENT_CLICKED,
                        NULL);

    lv_obj_t *back =
        gui_create_button(s_page,
                          "Back",
                          92,
                          38,
                          false);
    lv_obj_set_pos(back, 373, 250);
    lv_obj_add_event_cb(back,
                        back_click_cb,
                        LV_EVENT_CLICKED,
                        NULL);

    s_status = lv_label_create(s_page);
    lv_label_set_text(s_status,
                      "Enter student info, then enroll");
    lv_obj_set_style_text_color(s_status,
                                gui_color_muted(),
                                0);
    lv_obj_set_pos(s_status, 12, 246);
    lv_obj_set_width(s_status, 340);

    s_keyboard = lv_keyboard_create(s_page);
    lv_obj_set_size(s_keyboard, 480, 135);
    lv_obj_align(s_keyboard,
                 LV_ALIGN_BOTTOM_MID,
                 0,
                 0);
    lv_obj_set_style_bg_color(s_keyboard,
                              gui_color_surface(),
                              0);
    lv_obj_set_style_bg_color(s_keyboard,
                              lv_color_hex(GUI_COLOR_ACCENT_HEX),
                              LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_keyboard,
                                gui_color_primary(),
                                LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_keyboard,
                                  lv_color_hex(GUI_COLOR_BORDER_HEX),
                                  LV_PART_ITEMS);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_keyboard,
                        keyboard_ready_cb,
                        LV_EVENT_READY,
                        NULL);
    lv_obj_add_event_cb(s_keyboard,
                        keyboard_ready_cb,
                        LV_EVENT_CANCEL,
                        NULL);

    return s_page;
}

lv_obj_t *get_register_page(void)
{
    return s_page;
}

void gui_register_handle_face_event(const face_event_t *event)
{
    if (event == NULL ||
        s_status == NULL ||
        s_enroll_btn == NULL) {
        return;
    }

    char text[128];

    switch (event->type) {
        case FACE_EVENT_ENROLL_STARTED:
            lv_label_set_text(
                s_status,
                "Face detected, extracting feature...");
            lv_obj_set_style_text_color(s_status,
                                        gui_color_muted(),
                                        0);
            break;

        case FACE_EVENT_ENROLL_SUCCESS:
            snprintf(text,
                     sizeof(text),
                     "Enrollment OK. Face ID: %d",
                     event->face_id);
            lv_label_set_text(s_status, text);
            lv_obj_set_style_text_color(s_status,
                                        gui_color_success(),
                                        0);
            lv_obj_clear_state(s_enroll_btn,
                               LV_STATE_DISABLED);
            lv_textarea_set_text(s_student_id, "");
            lv_textarea_set_text(s_name, "");
            break;

        case FACE_EVENT_ENROLL_FAILED:
            switch (event->enroll_fail_reason) {
                case FACE_ENROLL_FAIL_DUPLICATE_STUDENT:
                    lv_label_set_text(
                        s_status,
                        "Student ID already enrolled");
                    break;
                case FACE_ENROLL_FAIL_DUPLICATE_FACE:
                    lv_label_set_text(
                        s_status,
                        "This face is already enrolled");
                    break;
                case FACE_ENROLL_FAIL_NO_FACE:
                    lv_label_set_text(
                        s_status,
                        "No face detected, try again");
                    break;
                case FACE_ENROLL_FAIL_MULTI_FACE:
                    lv_label_set_text(
                        s_status,
                        "Keep only one face in frame");
                    break;
                case FACE_ENROLL_FAIL_STORAGE:
                    lv_label_set_text(
                        s_status,
                        "Storage failed; face DB rolled back");
                    break;
                default:
                    lv_label_set_text(
                        s_status,
                        "Enrollment failed, try again");
                    break;
            }

            lv_obj_set_style_text_color(s_status,
                                        gui_color_warning(),
                                        0);
            lv_obj_clear_state(s_enroll_btn,
                               LV_STATE_DISABLED);
            break;

        default:
            break;
    }
}

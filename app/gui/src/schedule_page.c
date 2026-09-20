#include "gui_common.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"
#include "storage.h"
#include "timetable.h"

#define SCHEDULE_ROWS_PER_PAGE 5
#define SCHEDULE_TABLE_ROWS    (SCHEDULE_ROWS_PER_PAGE + 1)
#define SCHEDULE_TABLE_COLS    4

static lv_obj_t *s_page = NULL;
static lv_obj_t *s_info_label = NULL;
static lv_obj_t *s_table = NULL;
static lv_obj_t *s_page_label = NULL;
static lv_obj_t *s_prev_btn = NULL;
static lv_obj_t *s_next_btn = NULL;
static uint32_t s_page_index = 0;
static uint32_t s_page_count = 1;

static void minute_text(uint16_t minute, char out[6])
{
    uint16_t h = (minute / 60U) % 24U;
    uint16_t m = minute % 60U;
    snprintf(out, 6, "%02u:%02u", h, m);
}

static void update_nav_state(void)
{
    if (s_prev_btn == NULL || s_next_btn == NULL || s_page_label == NULL) {
        return;
    }

    if (s_page_index == 0) {
        lv_obj_add_state(s_prev_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(s_prev_btn, LV_STATE_DISABLED);
    }

    if (s_page_index + 1U >= s_page_count) {
        lv_obj_add_state(s_next_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(s_next_btn, LV_STATE_DISABLED);
    }

    lv_label_set_text_fmt(s_page_label,
                          "%lu / %lu",
                          (unsigned long)(s_page_index + 1U),
                          (unsigned long)s_page_count);
}

static void back_cb(lv_event_t *event)
{
    (void)event;
    gui_show_page(GUI_PAGE_HOME);
}

static void refresh_cb(lv_event_t *event)
{
    (void)event;
    gui_schedule_refresh();
}

static void prev_cb(lv_event_t *event)
{
    (void)event;
    if (s_page_index > 0) {
        s_page_index--;
        gui_schedule_refresh();
    }
}

static void next_cb(lv_event_t *event)
{
    (void)event;
    if (s_page_index + 1U < s_page_count) {
        s_page_index++;
        gui_schedule_refresh();
    }
}

lv_obj_t *create_schedule_page(void)
{
    s_page = lv_obj_create(NULL);
    gui_style_screen(s_page);

    lv_obj_t *title = gui_create_title(s_page, "Today Schedule");
    lv_obj_set_pos(title, 4, 0);

    s_info_label = lv_label_create(s_page);
    lv_obj_set_pos(s_info_label, 4, 25);
    lv_obj_set_width(s_info_label, 450);
    lv_obj_set_style_text_color(s_info_label, gui_color_muted(), 0);
    lv_label_set_text(s_info_label, "Waiting for timetable...");

    s_table = lv_table_create(s_page);
    lv_obj_set_pos(s_table, 4, 47);
    lv_obj_set_size(s_table, 452, 202);
    gui_style_table(s_table);
    lv_table_set_row_cnt(s_table, SCHEDULE_TABLE_ROWS);
    lv_table_set_col_cnt(s_table, SCHEDULE_TABLE_COLS);
    lv_table_set_col_width(s_table, 0, 52);
    lv_table_set_col_width(s_table, 1, 178);
    lv_table_set_col_width(s_table, 2, 70);
    lv_table_set_col_width(s_table, 3, 152);
    lv_table_set_cell_value(s_table, 0, 0, "P");
    lv_table_set_cell_value(s_table, 0, 1, "Course");
    lv_table_set_cell_value(s_table, 0, 2, "Class");
    lv_table_set_cell_value(s_table, 0, 3, "Time");

    lv_obj_t *back = gui_create_button(s_page, "Back", 72, 40, false);
    lv_obj_set_pos(back, 4, 263);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);

    s_prev_btn = gui_create_button(s_page, "Prev", 72, 40, false);
    lv_obj_set_pos(s_prev_btn, 84, 263);
    lv_obj_add_event_cb(s_prev_btn, prev_cb, LV_EVENT_CLICKED, NULL);

    s_page_label = lv_label_create(s_page);
    lv_obj_set_width(s_page_label, 82);
    lv_obj_set_style_text_align(s_page_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_page_label, gui_color_primary(), 0);
    lv_obj_set_pos(s_page_label, 164, 275);
    lv_label_set_text(s_page_label, "1 / 1");

    s_next_btn = gui_create_button(s_page, "Next", 72, 40, false);
    lv_obj_set_pos(s_next_btn, 254, 263);
    lv_obj_add_event_cb(s_next_btn, next_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *refresh = gui_create_button(s_page, "Refresh", 122, 40, true);
    lv_obj_set_pos(refresh, 334, 263);
    lv_obj_add_event_cb(refresh, refresh_cb, LV_EVENT_CLICKED, NULL);

    return s_page;
}

lv_obj_t *get_schedule_page(void)
{
    return s_page;
}

void gui_schedule_refresh(void)
{
    if (s_table == NULL ||
        s_info_label == NULL) {
        return;
    }

    for (int row = 1;
         row < SCHEDULE_TABLE_ROWS;
         ++row) {
        for (int col = 0;
             col < SCHEDULE_TABLE_COLS;
             ++col) {
            lv_table_set_cell_value(
                s_table,
                row,
                col,
                "");
        }
    }

    time_t now = time(NULL);
    char today[TIMETABLE_DATE_LEN] = {0};

    if (timetable_format_local_date(
            now,
            today) != ESP_OK) {
        lv_label_set_text(
            s_info_label,
            "Time not synchronized");

        lv_table_set_cell_value(
            s_table,
            1,
            1,
            "Time required");

        s_page_count = 1;
        s_page_index = 0;
        update_nav_state();
        return;
    }

    timetable_info_t info = {0};
    (void)timetable_get_info(&info);

    timetable_day_t day = {0};
    esp_err_t err =
        timetable_get_day(&day);

    if (err == ESP_ERR_NOT_FOUND) {
        lv_label_set_text_fmt(
            s_info_label,
            "%s  No semester schedule",
            today);

        lv_table_set_cell_value(
            s_table,
            1,
            1,
            "Sync schedule");

        s_page_count = 1;
        s_page_index = 0;
        update_nav_state();
        return;
    }

    if (err != ESP_OK) {
        lv_label_set_text(
            s_info_label,
            "Timetable read failed");

        lv_table_set_cell_value(
            s_table,
            1,
            1,
            "Read failed");

        s_page_count = 1;
        s_page_index = 0;
        update_nav_state();
        return;
    }

    if (day.semester_week > 0) {
        lv_label_set_text_fmt(
            s_info_label,
            "%s  W%u  v%lu  %u classes",
            day.date,
            (unsigned)day.semester_week,
            (unsigned long)day.version,
            (unsigned)day.count);
    } else {
        lv_label_set_text_fmt(
            s_info_label,
            "%s  override/off-term  %u classes",
            day.date,
            (unsigned)day.count);
    }

    s_page_count =
        day.count == 0
            ? 1U
            : ((uint32_t)day.count +
               SCHEDULE_ROWS_PER_PAGE -
               1U) /
                  SCHEDULE_ROWS_PER_PAGE;

    if (s_page_index >=
        s_page_count) {
        s_page_index =
            s_page_count - 1U;
    }

    if (day.count == 0) {
        lv_table_set_cell_value(
            s_table,
            1,
            1,
            "No classes today");

        s_page_index = 0;
        update_nav_state();
        return;
    }

    timetable_entry_t current = {0};

    bool has_current =
        timetable_get_current_session(
            now,
            &current) == ESP_OK;

    uint32_t first =
        s_page_index *
        SCHEDULE_ROWS_PER_PAGE;

    for (uint32_t i = 0;
         i < SCHEDULE_ROWS_PER_PAGE;
         ++i) {
        uint32_t index = first + i;

        if (index >= day.count) {
            break;
        }

        const timetable_entry_t *entry =
            &day.entries[index];

        int row = (int)i + 1;

        char text[40];
        char start[6];
        char end[6];

        snprintf(
            text,
            sizeof(text),
            "%s%u",
            has_current &&
                    current.session_id ==
                        entry->session_id
                ? ">"
                : "",
            (unsigned)entry->period);

        lv_table_set_cell_value(
            s_table,
            row,
            0,
            text);

        lv_table_set_cell_value(
            s_table,
            row,
            1,
            entry->course_name);

        snprintf(text,
                 sizeof(text),
                 "%d",
                 entry->class_id);

        lv_table_set_cell_value(
            s_table,
            row,
            2,
            text);

        minute_text(
            entry->start_minute,
            start);

        minute_text(
            entry->end_minute,
            end);

        snprintf(text,
                 sizeof(text),
                 "%s-%s",
                 start,
                 end);

        lv_table_set_cell_value(
            s_table,
            row,
            3,
            text);
    }

    update_nav_state();
}

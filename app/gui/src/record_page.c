#include "gui_common.h"

#include <stdio.h>
#include <time.h>

#include "lvgl.h"
#include "storage.h"

#define RECORD_ROWS_PER_PAGE 5
#define RECORD_TABLE_ROWS     (RECORD_ROWS_PER_PAGE + 1)
#define RECORD_TABLE_COLS     6

static lv_obj_t *s_page = NULL;
static lv_obj_t *s_table = NULL;
static lv_obj_t *s_page_label = NULL;
static lv_obj_t *s_prev_btn = NULL;
static lv_obj_t *s_next_btn = NULL;

static uint32_t s_page_index = 0;
static uint32_t s_page_count = 1;

static void format_time(int64_t timestamp, char *buf, size_t size)
{
    if (timestamp < STORAGE_TIME_VALID_EPOCH) {
        snprintf(buf, size, "--");
        return;
    }

    time_t t = (time_t)timestamp;
    struct tm tm_value = {0};
    localtime_r(&t, &tm_value);
    strftime(buf, size, "%m-%d %H:%M", &tm_value);
}

static const char *status_text(checkin_status_t status)
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
    gui_record_refresh();
}

static void prev_cb(lv_event_t *event)
{
    (void)event;

    if (s_page_index > 0) {
        s_page_index--;
        gui_record_refresh();
    }
}

static void next_cb(lv_event_t *event)
{
    (void)event;

    if (s_page_index + 1U < s_page_count) {
        s_page_index++;
        gui_record_refresh();
    }
}

lv_obj_t *create_record_page(void)
{
    s_page = lv_obj_create(NULL);
    gui_style_screen(s_page);

    lv_obj_t *title = gui_create_title(s_page, "Check-in Records");
    lv_obj_set_pos(title, 4, 0);

    lv_obj_t *subtitle = lv_label_create(s_page);
    lv_label_set_text(subtitle, "Today only - newest first");
    lv_obj_set_style_text_color(subtitle, gui_color_muted(), 0);
    lv_obj_set_pos(subtitle, 4, 25);

    s_table = lv_table_create(s_page);
    lv_obj_set_pos(s_table, 4, 47);
    lv_obj_set_size(s_table, 452, 202);
    gui_style_table(s_table);

    lv_table_set_row_cnt(s_table, RECORD_TABLE_ROWS);
    lv_table_set_col_cnt(s_table, RECORD_TABLE_COLS);

    lv_table_set_col_width(s_table, 0, 38);
    lv_table_set_col_width(s_table, 1, 72);
    lv_table_set_col_width(s_table, 2, 72);
    lv_table_set_col_width(s_table, 3, 52);
    lv_table_set_col_width(s_table, 4, 68);
    lv_table_set_col_width(s_table, 5, 126);

    lv_table_set_cell_value(s_table, 0, 0, "ID");
    lv_table_set_cell_value(s_table, 0, 1, "Student");
    lv_table_set_cell_value(s_table, 0, 2, "Name");
    lv_table_set_cell_value(s_table, 0, 3, "Class");
    lv_table_set_cell_value(s_table, 0, 4, "Status");
    lv_table_set_cell_value(s_table, 0, 5, "Time");

    lv_obj_t *back =
        gui_create_button(s_page, "Back", 72, 40, false);
    lv_obj_set_pos(back, 4, 263);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);

    s_prev_btn =
        gui_create_button(s_page, "Prev", 72, 40, false);
    lv_obj_set_pos(s_prev_btn, 84, 263);
    lv_obj_add_event_cb(s_prev_btn, prev_cb, LV_EVENT_CLICKED, NULL);

    s_page_label = lv_label_create(s_page);
    lv_obj_set_width(s_page_label, 82);
    lv_obj_set_style_text_align(s_page_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_page_label, gui_color_primary(), 0);
    lv_obj_set_pos(s_page_label, 164, 275);
    lv_label_set_text(s_page_label, "1 / 1");

    s_next_btn =
        gui_create_button(s_page, "Next", 72, 40, false);
    lv_obj_set_pos(s_next_btn, 254, 263);
    lv_obj_add_event_cb(s_next_btn, next_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *refresh =
        gui_create_button(s_page, "Refresh", 122, 40, true);
    lv_obj_set_pos(refresh, 334, 263);
    lv_obj_add_event_cb(refresh, refresh_cb, LV_EVENT_CLICKED, NULL);

    return s_page;
}

lv_obj_t *get_record_page(void)
{
    return s_page;
}

void gui_record_refresh(void)
{
    if (s_table == NULL) {
        return;
    }

    checkin_record_t records[RECORD_ROWS_PER_PAGE];
    uint32_t total = 0;
    uint32_t offset =
        s_page_index * (uint32_t)RECORD_ROWS_PER_PAGE;

    int count = storage_checkin_list_page_today(records,
                                          RECORD_ROWS_PER_PAGE,
                                          offset,
                                          &total);

    s_page_count =
        total == 0
            ? 1U
            : (total + RECORD_ROWS_PER_PAGE - 1U) /
                  RECORD_ROWS_PER_PAGE;

    if (s_page_index >= s_page_count) {
        s_page_index = s_page_count - 1U;
        offset =
            s_page_index * (uint32_t)RECORD_ROWS_PER_PAGE;
        count = storage_checkin_list_page_today(records,
                                          RECORD_ROWS_PER_PAGE,
                                          offset,
                                          &total);
    }

    for (int row = 1; row < RECORD_TABLE_ROWS; ++row) {
        for (int col = 0; col < RECORD_TABLE_COLS; ++col) {
            lv_table_set_cell_value(s_table, row, col, "");
        }
    }

    if (count <= 0) {
        lv_table_set_cell_value(s_table, 1, 1, "No records");
        update_nav_state();
        return;
    }

    for (int i = 0; i < count; ++i) {
        int row = i + 1;
        char buffer[40];
        char time_text[32];

        snprintf(buffer,
                 sizeof(buffer),
                 "%lu",
                 (unsigned long)records[i].record_id);
        lv_table_set_cell_value(s_table, row, 0, buffer);

        lv_table_set_cell_value(s_table,
                                row,
                                1,
                                records[i].student_id);
        lv_table_set_cell_value(s_table,
                                row,
                                2,
                                records[i].name);

        snprintf(buffer,
                 sizeof(buffer),
                 "%d",
                 records[i].class_id);
        lv_table_set_cell_value(s_table, row, 3, buffer);

        lv_table_set_cell_value(s_table,
                                row,
                                4,
                                status_text(records[i].status));

        format_time(records[i].timestamp,
                    time_text,
                    sizeof(time_text));
        lv_table_set_cell_value(s_table,
                                row,
                                5,
                                time_text);
    }

    update_nav_state();
}

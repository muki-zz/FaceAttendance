#include "gui_common.h"

#include <stdio.h>

#include "lvgl.h"
#include "storage.h"

#define STAT_ROWS_PER_PAGE 4
#define STAT_METRIC_COUNT  8
#define STAT_PAGE_COUNT    ((STAT_METRIC_COUNT + STAT_ROWS_PER_PAGE - 1) / STAT_ROWS_PER_PAGE)

typedef struct {
    const char *name;
    char value[32];
} stat_metric_t;

static lv_obj_t *s_page = NULL;
static lv_obj_t *s_table = NULL;
static lv_obj_t *s_page_label = NULL;
static lv_obj_t *s_prev_btn = NULL;
static lv_obj_t *s_next_btn = NULL;
static uint32_t s_page_index = 0;

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

    if (s_page_index + 1U >= STAT_PAGE_COUNT) {
        lv_obj_add_state(s_next_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(s_next_btn, LV_STATE_DISABLED);
    }

    lv_label_set_text_fmt(s_page_label,
                          "%lu / %u",
                          (unsigned long)(s_page_index + 1U),
                          (unsigned)STAT_PAGE_COUNT);
}

static void back_cb(lv_event_t *event)
{
    (void)event;
    gui_show_page(GUI_PAGE_HOME);
}

static void refresh_cb(lv_event_t *event)
{
    (void)event;
    gui_stat_refresh();
}

static void prev_cb(lv_event_t *event)
{
    (void)event;

    if (s_page_index > 0) {
        s_page_index--;
        gui_stat_refresh();
    }
}

static void next_cb(lv_event_t *event)
{
    (void)event;

    if (s_page_index + 1U < STAT_PAGE_COUNT) {
        s_page_index++;
        gui_stat_refresh();
    }
}

lv_obj_t *create_stat_page(void)
{
    s_page = lv_obj_create(NULL);
    gui_style_screen(s_page);

    lv_obj_t *title = gui_create_title(s_page, "Attendance Stats");
    lv_obj_set_pos(title, 4, 0);

    lv_obj_t *subtitle = lv_label_create(s_page);
    lv_label_set_text(subtitle, "Today only - local summary");
    lv_obj_set_style_text_color(subtitle, gui_color_muted(), 0);
    lv_obj_set_pos(subtitle, 4, 25);

    s_table = lv_table_create(s_page);
    lv_obj_set_pos(s_table, 24, 56);
    lv_obj_set_size(s_table, 412, 184);
    gui_style_table(s_table);

    lv_table_set_row_cnt(s_table, STAT_ROWS_PER_PAGE + 1);
    lv_table_set_col_cnt(s_table, 2);
    lv_table_set_col_width(s_table, 0, 260);
    lv_table_set_col_width(s_table, 1, 140);
    lv_table_set_cell_value(s_table, 0, 0, "Metric");
    lv_table_set_cell_value(s_table, 0, 1, "Value");

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
    lv_label_set_text(s_page_label, "1 / 2");

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

lv_obj_t *get_stat_page(void)
{
    return s_page;
}

void gui_stat_refresh(void)
{
    if (s_table == NULL) {
        return;
    }

    attendance_stats_t stats = {0};
    if (storage_get_stats_today(&stats) != ESP_OK) {
        lv_table_set_cell_value(s_table, 1, 0, "Storage");
        lv_table_set_cell_value(s_table, 1, 1, "Read failed");
        return;
    }

    uint32_t valid_timed =
        stats.checkin_count >= stats.unsynced_count
            ? stats.checkin_count - stats.unsynced_count
            : 0U;

    uint32_t classified =
        stats.normal_count + stats.late_count + stats.absent_count;

    unsigned on_time_percent =
        classified > 0U
            ? (unsigned)((stats.normal_count * 100U) / classified)
            : 0U;

    stat_metric_t metrics[STAT_METRIC_COUNT] = {
        {.name = "Registered students"},
        {.name = "Today check-ins"},
        {.name = "Normal"},
        {.name = "Late"},
        {.name = "Absent"},
        {.name = "Time unsynced"},
        {.name = "Valid timed records"},
        {.name = "On-time rate"},
    };

    snprintf(metrics[0].value,
             sizeof(metrics[0].value),
             "%lu",
             (unsigned long)stats.student_count);
    snprintf(metrics[1].value,
             sizeof(metrics[1].value),
             "%lu",
             (unsigned long)stats.checkin_count);
    snprintf(metrics[2].value,
             sizeof(metrics[2].value),
             "%lu",
             (unsigned long)stats.normal_count);
    snprintf(metrics[3].value,
             sizeof(metrics[3].value),
             "%lu",
             (unsigned long)stats.late_count);
    snprintf(metrics[4].value,
             sizeof(metrics[4].value),
             "%lu",
             (unsigned long)stats.absent_count);
    snprintf(metrics[5].value,
             sizeof(metrics[5].value),
             "%lu",
             (unsigned long)stats.unsynced_count);
    snprintf(metrics[6].value,
             sizeof(metrics[6].value),
             "%lu",
             (unsigned long)valid_timed);
    snprintf(metrics[7].value,
             sizeof(metrics[7].value),
             "%u%%",
             on_time_percent);

    uint32_t first = s_page_index * STAT_ROWS_PER_PAGE;

    for (uint32_t i = 0; i < STAT_ROWS_PER_PAGE; ++i) {
        uint32_t metric_index = first + i;
        uint32_t row = i + 1U;

        if (metric_index < STAT_METRIC_COUNT) {
            lv_table_set_cell_value(s_table,
                                    (uint16_t)row,
                                    0,
                                    metrics[metric_index].name);
            lv_table_set_cell_value(s_table,
                                    (uint16_t)row,
                                    1,
                                    metrics[metric_index].value);
        } else {
            lv_table_set_cell_value(s_table,
                                    (uint16_t)row,
                                    0,
                                    "");
            lv_table_set_cell_value(s_table,
                                    (uint16_t)row,
                                    1,
                                    "");
        }
    }

    update_nav_state();
}

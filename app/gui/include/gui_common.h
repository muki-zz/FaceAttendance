#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "face_service.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GUI_PAGE_HOME = 0,
    GUI_PAGE_REGISTER,
    GUI_PAGE_RECORD,
    GUI_PAGE_STAT,
    GUI_PAGE_SCHEDULE,
} gui_page_id_t;

/* Soft blue palette derived from the supplied 4-color reference image. */
#define GUI_COLOR_BG_HEX          0xF7FBFC
#define GUI_COLOR_SURFACE_HEX     0xFFFFFF
#define GUI_COLOR_SURFACE_ALT_HEX 0xD6E6F2
#define GUI_COLOR_PRIMARY_HEX     0x769FCD
#define GUI_COLOR_TEXT_HEX        0x445463
#define GUI_COLOR_MUTED_HEX       0x748391
#define GUI_COLOR_ACCENT_HEX      0xB9D7EA
#define GUI_COLOR_ACCENT_DARK_HEX 0x9FC6DF
#define GUI_COLOR_BORDER_HEX      0xC8D9E7
#define GUI_COLOR_SUCCESS_HEX     0x5D8E6B
#define GUI_COLOR_WARNING_HEX     0xD19263

lv_color_t gui_color_bg(void);
lv_color_t gui_color_surface(void);
lv_color_t gui_color_surface_alt(void);
lv_color_t gui_color_primary(void);
lv_color_t gui_color_text(void);
lv_color_t gui_color_muted(void);
lv_color_t gui_color_success(void);
lv_color_t gui_color_warning(void);

void gui_style_screen(lv_obj_t *screen);
void gui_style_button(lv_obj_t *button, bool primary);
void gui_style_input(lv_obj_t *input);
void gui_style_table(lv_obj_t *table);
void gui_table_draw_event_cb(lv_event_t *event);
lv_obj_t *gui_create_title(lv_obj_t *parent, const char *text);
lv_obj_t *gui_create_button(lv_obj_t *parent,
                            const char *text,
                            lv_coord_t width,
                            lv_coord_t height,
                            bool primary);

lv_obj_t *create_home_page(void);
lv_obj_t *create_register_page(void);
lv_obj_t *create_record_page(void);
lv_obj_t *create_stat_page(void);
lv_obj_t *create_schedule_page(void);

lv_obj_t *get_home_page(void);
lv_obj_t *get_register_page(void);
lv_obj_t *get_record_page(void);
lv_obj_t *get_stat_page(void);
lv_obj_t *get_schedule_page(void);

void gui_show_page(gui_page_id_t page_id);
void gui_nav_event_cb(lv_event_t *event);

void gui_home_handle_face_event(const face_event_t *event);
void gui_register_handle_face_event(const face_event_t *event);
void gui_record_refresh(void);
void gui_stat_refresh(void);
void gui_schedule_refresh(void);

#ifdef __cplusplus
}
#endif

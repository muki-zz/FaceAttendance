#include "gui_common.h"

#include <stdint.h>

lv_color_t gui_color_bg(void)
{
    return lv_color_hex(GUI_COLOR_BG_HEX);
}

lv_color_t gui_color_surface(void)
{
    return lv_color_hex(GUI_COLOR_SURFACE_HEX);
}

lv_color_t gui_color_surface_alt(void)
{
    return lv_color_hex(GUI_COLOR_SURFACE_ALT_HEX);
}

lv_color_t gui_color_primary(void)
{
    return lv_color_hex(GUI_COLOR_PRIMARY_HEX);
}

lv_color_t gui_color_text(void)
{
    return lv_color_hex(GUI_COLOR_TEXT_HEX);
}

lv_color_t gui_color_muted(void)
{
    return lv_color_hex(GUI_COLOR_MUTED_HEX);
}

lv_color_t gui_color_success(void)
{
    return lv_color_hex(GUI_COLOR_SUCCESS_HEX);
}

lv_color_t gui_color_warning(void)
{
    return lv_color_hex(GUI_COLOR_WARNING_HEX);
}

void gui_style_screen(lv_obj_t *screen)
{
    if (screen == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(screen, gui_color_bg(), 0);
    lv_obj_set_style_bg_grad_color(screen, gui_color_surface_alt(), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 10, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_text_color(screen, gui_color_text(), 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

void gui_style_button(lv_obj_t *button, bool primary)
{
    if (button == NULL) {
        return;
    }

    lv_color_t bg = primary ? gui_color_primary()
                            : lv_color_hex(GUI_COLOR_ACCENT_HEX);
    lv_color_t grad = primary ? lv_color_hex(0x628DBE)
                              : lv_color_hex(GUI_COLOR_ACCENT_DARK_HEX);
    lv_color_t pressed = primary ? lv_color_hex(0x5C86B7)
                                 : lv_color_hex(0x95BED8);

    lv_obj_set_style_bg_color(button, bg, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(button, grad, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(button, LV_GRAD_DIR_HOR, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(button, pressed, LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_color(button, pressed, LV_STATE_PRESSED);

    lv_obj_set_style_bg_color(button, gui_color_surface_alt(), LV_STATE_DISABLED);
    lv_obj_set_style_bg_grad_color(button, gui_color_surface_alt(), LV_STATE_DISABLED);

    lv_obj_set_style_text_color(button,
                                primary ? lv_color_hex(0xF7FBFC)
                                        : gui_color_text(),
                                LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(button,
                                gui_color_muted(),
                                LV_STATE_DISABLED);

    lv_obj_set_style_border_color(button,
                                  primary ? lv_color_hex(0x88ADD3)
                                          : lv_color_hex(0xA9CDE3),
                                  0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_radius(button, 16, 0);
    lv_obj_set_style_shadow_width(button, 10, 0);
    lv_obj_set_style_shadow_opa(button, LV_OPA_20, 0);
    lv_obj_set_style_shadow_color(button,
                                  primary ? lv_color_hex(0xC3D8EA)
                                          : lv_color_hex(0xD7E8F2),
                                  0);
    lv_obj_set_style_shadow_ofs_y(button, 3, 0);
    lv_obj_set_style_pad_all(button, 7, 0);
}

void gui_style_input(lv_obj_t *input)
{
    if (input == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(input, gui_color_surface(), 0);
    lv_obj_set_style_bg_opa(input, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(input, gui_color_text(), 0);
    lv_obj_set_style_border_color(input, lv_color_hex(GUI_COLOR_ACCENT_HEX), 0);
    lv_obj_set_style_border_width(input, 1, 0);
    lv_obj_set_style_radius(input, 12, 0);
    lv_obj_set_style_shadow_width(input, 8, 0);
    lv_obj_set_style_shadow_opa(input, LV_OPA_10, 0);
    lv_obj_set_style_shadow_color(input, lv_color_hex(0xD7E8F2), 0);
    lv_obj_set_style_pad_left(input, 10, 0);
    lv_obj_set_style_pad_right(input, 10, 0);

    lv_obj_set_style_border_color(input, gui_color_primary(), LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_width(input, 12, LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_opa(input, LV_OPA_20, LV_STATE_FOCUSED);
}

void gui_table_draw_event_cb(lv_event_t *event)
{
    lv_obj_t *table = lv_event_get_target(event);
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_param(event);

    if (table == NULL || dsc == NULL || dsc->part != LV_PART_ITEMS) {
        return;
    }

    uint16_t col_count = lv_table_get_col_cnt(table);
    if (col_count == 0) {
        return;
    }

    uint32_t row = dsc->id / col_count;

    if (row == 0) {
        dsc->rect_dsc->bg_color = gui_color_primary();
        dsc->rect_dsc->bg_opa = LV_OPA_COVER;
        dsc->label_dsc->color = lv_color_hex(0xF7FBFC);
        dsc->label_dsc->align = LV_TEXT_ALIGN_CENTER;
    } else {
        dsc->rect_dsc->bg_color =
            (row % 2U) == 0U ? lv_color_hex(0xEEF6FA)
                             : lv_color_hex(0xF7FBFC);
        dsc->rect_dsc->bg_opa = LV_OPA_COVER;
        dsc->label_dsc->color = gui_color_text();
        dsc->label_dsc->align = LV_TEXT_ALIGN_CENTER;
    }
}

void gui_style_table(lv_obj_t *table)
{
    if (table == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(table, gui_color_surface(), 0);
    lv_obj_set_style_bg_opa(table, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(table, lv_color_hex(GUI_COLOR_BORDER_HEX), 0);
    lv_obj_set_style_border_width(table, 1, 0);
    lv_obj_set_style_radius(table, 14, 0);
    lv_obj_set_style_shadow_width(table, 10, 0);
    lv_obj_set_style_shadow_opa(table, LV_OPA_10, 0);
    lv_obj_set_style_shadow_color(table, lv_color_hex(0xD7E8F2), 0);
    lv_obj_set_style_pad_all(table, 0, 0);

    lv_obj_set_style_border_color(table,
                                  lv_color_hex(GUI_COLOR_BORDER_HEX),
                                  LV_PART_ITEMS);
    lv_obj_set_style_border_width(table, 1, LV_PART_ITEMS);
    lv_obj_set_style_pad_top(table, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(table, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(table, 4, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(table, 4, LV_PART_ITEMS);

    lv_obj_add_event_cb(table,
                        gui_table_draw_event_cb,
                        LV_EVENT_DRAW_PART_BEGIN,
                        NULL);
}

lv_obj_t *gui_create_title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, gui_color_primary(), 0);
    lv_obj_set_style_text_letter_space(label, 1, 0);
    return label;
}

lv_obj_t *gui_create_button(lv_obj_t *parent,
                            const char *text,
                            lv_coord_t width,
                            lv_coord_t height,
                            bool primary)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, width, height);
    gui_style_button(button, primary);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label,
                                primary ? lv_color_hex(0xF7FBFC)
                                        : gui_color_text(),
                                0);
    lv_obj_center(label);

    return button;
}

static lv_obj_t *page_from_id(gui_page_id_t page_id)
{
    switch (page_id) {
        case GUI_PAGE_REGISTER:
            return get_register_page();
        case GUI_PAGE_RECORD:
            return get_record_page();
        case GUI_PAGE_STAT:
            return get_stat_page();
        case GUI_PAGE_SCHEDULE:
            return get_schedule_page();
        case GUI_PAGE_HOME:
        default:
            return get_home_page();
    }
}

void gui_show_page(gui_page_id_t page_id)
{
    lv_obj_t *page = page_from_id(page_id);
    if (page == NULL) {
        return;
    }

    bool preview =
        (page_id == GUI_PAGE_HOME || page_id == GUI_PAGE_REGISTER);
    face_service_set_preview_enabled(preview);

    if (page_id == GUI_PAGE_RECORD) {
        gui_record_refresh();
    } else if (page_id == GUI_PAGE_STAT) {
        gui_stat_refresh();
    } else if (page_id == GUI_PAGE_SCHEDULE) {
        gui_schedule_refresh();
    }

    lv_scr_load(page);
}

void gui_nav_event_cb(lv_event_t *event)
{
    gui_page_id_t page_id =
        (gui_page_id_t)(intptr_t)lv_event_get_user_data(event);
    gui_show_page(page_id);
}

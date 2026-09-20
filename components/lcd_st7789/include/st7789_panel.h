#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "lcd_display.h"

typedef struct {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
} st7789_panel_t;

esp_err_t st7789_panel_create(const lcd_display_config_t *config, st7789_panel_t *ret_panel);
esp_err_t st7789_panel_delete(st7789_panel_t *panel);

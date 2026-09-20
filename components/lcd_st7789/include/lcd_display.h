#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "pca9557.h"

/* 共用PCA9557由板级创建和持有。LCD不初始化或删除I2C/PCA9557。 */

/** Board-specific pins and ST7789 panel settings. */
typedef struct {
    spi_host_device_t spi_host;
    int sclk_gpio_num;
    int mosi_gpio_num;
    int cs_gpio_num;
    int dc_gpio_num;
    int reset_gpio_num;      ///< -1 if reset is tied to the board reset circuit
    int backlight_gpio_num;  ///< -1 if the board has no controllable backlight
    bool cs_via_pca9557;     ///< true: PCA9557 IO0 is LCD_CS; SPI hardware CS is disabled
    pca9557_handle_t expander; ///< 借用；cs_via_pca9557=true时必填，只操作IO0
    uint16_t hor_res;
    uint16_t ver_res;
    uint32_t pclk_hz;        ///< SPI clock; begin with 20 MHz, then validate signal integrity
    uint16_t x_gap;
    uint16_t y_gap;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    bool invert_color;
    bool backlight_active_high;
} lcd_display_config_t;

typedef struct lcd_display_t *lcd_display_handle_t;

/** Creates an exclusive SPI bus and panel. External CS stays low while alive.
 * GPIO CS mode ignores expander. PCA mode requires cs_gpio_num=-1.
 * On failure output is NULL; rollback failure uses ESP_ERROR_CHECK (fatal).
 * Do not share SPI traffic with other devices in external-CS mode.
 */
esp_err_t lcd_display_create(const lcd_display_config_t *config, lcd_display_handle_t *ret_display);

/** Synchronously transfers an RGB565 rectangle. Coordinates use a half-open end point. */
esp_err_t lcd_display_draw_bitmap(lcd_display_handle_t display, int x_start, int y_start,
                                  int x_end, int y_end, const void *color_data);

/** Draws a solid RGB565 color; this synchronous convenience operation uses a small line buffer. */
esp_err_t lcd_display_fill(lcd_display_handle_t display, uint16_t rgb565);

/** Set absolute panel orientation at runtime (same meanings as create config).
 * Automatically swaps logical width/height and x/y gaps when swap_xy changes.
 * Does not rotate existing GRAM contents: redraw after success. Task context only.
 * Do not call delete concurrently with any display operation.
 */
esp_err_t lcd_display_set_direction(lcd_display_handle_t display, bool swap_xy,
                                    bool mirror_x, bool mirror_y);

/** Return current logical dimensions, including runtime orientation changes. */
esp_err_t lcd_display_get_size(lcd_display_handle_t display, uint16_t *width, uint16_t *height);

esp_err_t lcd_display_set_backlight(lcd_display_handle_t display, bool on);
/* Stop all users before delete. On error keep the handle and retry delete;
 * no further drawing is allowed. Never deletes the borrowed PCA/I2C handles. */
esp_err_t lcd_display_delete(lcd_display_handle_t display);

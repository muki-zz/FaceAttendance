/**
 * @file app_ctx.h
 * @brief Board/application hardware context.
 */
#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "i2c_bus.h"
#include "pca9557.h"
#include "lcd_display.h"
#include "touch_ft6x36.h"
#include "camera_device.h"

typedef struct {
    i2c_bus_handle_t i2c_bus;
    pca9557_handle_t io_expander;
    lcd_display_handle_t lcd;
    touch_ft6x36_handle_t touch_device;
    camera_device_t camera;

    /* App-level lock only. Components remain untouched. It serializes LVGL flush and camera preview. */
    SemaphoreHandle_t lcd_mutex;
} app_ctx_t;

esp_err_t app_ctx_create(app_ctx_t *ctx);
void app_ctx_delete(app_ctx_t *ctx);

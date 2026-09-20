#include "app_lvgl.h"

#include <assert.h>
#include <stdint.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "face_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gui_common.h"
#include "lvgl.h"

static const char *TAG = "lvgl_app";
static app_ctx_t *s_ctx = NULL;

#if LV_COLOR_DEPTH != 16
#error "This LCD adapter requires LV_COLOR_DEPTH=16"
#endif

#if !LV_TICK_CUSTOM
static void tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(1);
}
#endif

static void lcd_flush_cb(lv_disp_drv_t *disp_drv,
                         const lv_area_t *area,
                         lv_color_t *color_map)
{
    assert(s_ctx != NULL && s_ctx->lcd != NULL);

    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;

#if !LV_COLOR_16_SWAP
    uint8_t *bytes = (uint8_t *)color_map;
    for (int32_t i = 0; i < w * h; ++i) {
        uint8_t tmp = bytes[2 * i];
        bytes[2 * i] = bytes[2 * i + 1];
        bytes[2 * i + 1] = tmp;
    }
#else
    (void)w;
    (void)h;
#endif

    if (s_ctx->lcd_mutex != NULL) {
        xSemaphoreTake(s_ctx->lcd_mutex, portMAX_DELAY);
    }
    esp_err_t err = lcd_display_draw_bitmap(s_ctx->lcd,
                                            area->x1,
                                            area->y1,
                                            area->x2 + 1,
                                            area->y2 + 1,
                                            color_map);
    if (s_ctx->lcd_mutex != NULL) {
        xSemaphoreGive(s_ctx->lcd_mutex);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lcd flush failed: %s", esp_err_to_name(err));
    }
    lv_disp_flush_ready(disp_drv);
}

static void touch_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    (void)indev_drv;
    static uint16_t last_x = 0;
    static uint16_t last_y = 0;

    touch_ft6x36_sample_t sample = {0};
    esp_err_t err = touch_ft6x36_read(s_ctx->touch_device, &sample);
    if (err == ESP_OK && sample.count > 0) {
        last_x = sample.points[0].x;
        last_y = sample.points[0].y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
    data->point.x = last_x;
    data->point.y = last_y;
}

static void dispatch_face_events(void)
{
    QueueHandle_t queue = face_service_get_event_queue();
    if (queue == NULL) {
        return;
    }

    face_event_t event;
    while (xQueueReceive(queue, &event, 0) == pdPASS) {
        gui_home_handle_face_event(&event);
        gui_register_handle_face_event(&event);
    }
}

void app_lvgl_run(app_ctx_t *ctx)
{
    assert(ctx != NULL && ctx->lcd != NULL && ctx->touch_device != NULL);
    s_ctx = ctx;

    lv_init();

#if !LV_TICK_CUSTOM
    static esp_timer_handle_t tick_timer;
    const esp_timer_create_args_t tick_args = {
        .callback = tick_cb,
        .name = "lvgl_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 1000));
#endif

    uint16_t width = 0;
    uint16_t height = 0;
    ESP_ERROR_CHECK(lcd_display_get_size(ctx->lcd, &width, &height));

    size_t buffer_pixels = (size_t)width * 40U;
    lv_color_t *buffer = (lv_color_t *)heap_caps_malloc(buffer_pixels * sizeof(lv_color_t),
                                                        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(buffer != NULL);

    static lv_disp_draw_buf_t draw_buf;
    static lv_disp_drv_t disp_drv;
    lv_disp_draw_buf_init(&draw_buf, buffer, NULL, (uint32_t)buffer_pixels);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = width;
    disp_drv.ver_res = height;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.flush_cb = lcd_flush_cb;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
    assert(disp != NULL);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = touch_read_cb;
    assert(lv_indev_drv_register(&indev_drv) != NULL);

    create_home_page();
    create_register_page();
    create_record_page();
    create_stat_page();
    create_schedule_page();
    gui_show_page(GUI_PAGE_HOME);

    while (1) {
        dispatch_face_events();
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

#include "app_gui.h"

#include "app_ctx.h"
#include "app_lvgl.h"
#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "gui";

void gui_task_entry(void *arg)
{
    app_ctx_t *ctx = (app_ctx_t *)arg;
    if (ctx == NULL || ctx->lcd == NULL || ctx->touch_device == NULL) {
        ESP_LOGE(TAG, "invalid app context");
        vTaskDelete(NULL);
        return;
    }

    ESP_ERROR_CHECK(touch_ft6x36_set_mirror(ctx->touch_device, true, false));

    app_lvgl_run(ctx);
    vTaskDelete(NULL);
}

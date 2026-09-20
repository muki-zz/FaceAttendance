#include "board_camera.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void board_camera_default_config(camera_config_t *config)
{
    if (!config) return;
    *config = (camera_config_t) {
        .pin_pwdn = -1,  /* PCA9557 IO2，绝不是GPIO2 */
        .pin_reset = -1, /* J6 RESET是系统复位网络，不能用GPIO驱动 */
        .pin_sccb_sda = -1, .pin_sccb_scl = -1,
        .sccb_i2c_port = I2C_NUM_0,
        .pin_xclk = 5, .pin_pclk = 7, .pin_vsync = 3, .pin_href = 46,
        .pin_d0 = 16, .pin_d1 = 18, .pin_d2 = 8, .pin_d3 = 17,
        .pin_d4 = 15, .pin_d5 = 6, .pin_d6 = 4, .pin_d7 = 9,
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0, .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_RGB565,
        .frame_size = FRAMESIZE_QVGA, /* GC0308，320x240 */
        .jpeg_quality = 12,
        .fb_count = 1,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
    };
}

esp_err_t board_camera_wake(pca9557_handle_t expander, bool active_high)
{
    ESP_RETURN_ON_FALSE(expander, ESP_ERR_INVALID_ARG, "board_camera", "PCA handle required");
    /* 写初始锁存值后再设输出，仅修改IO2。 */
    ESP_RETURN_ON_ERROR(pca9557_config_output(expander, 2, active_high), "board_camera", "PWDN assert");    //设置为输出模式
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(pca9557_set_level(expander, 2, !active_high), "board_camera", "PWDN release");      //IO2输出低电平
    vTaskDelay(pdMS_TO_TICKS(20));
    return ESP_OK;
}

esp_err_t board_camera_sleep(pca9557_handle_t expander, bool active_high)
{
    ESP_RETURN_ON_FALSE(expander, ESP_ERR_INVALID_ARG, "board_camera", "PCA handle required");
    return pca9557_config_output(expander, 2, active_high);
}

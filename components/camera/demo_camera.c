#include "demo_camera.h"
#include "camera_device.h"
#include "esp_check.h"

esp_err_t demo_camera_capture(i2c_bus_handle_t bus, pca9557_handle_t expander)
{
    const char *TAG = "camera_demo";
    esp_err_t ret = ESP_OK;
    camera_device_t camera = {0};
    camera_fb_t *frame = NULL;
    camera_device_config_t config;
    camera_device_default_config(&config);
    /* GC0308不支持硬件JPEG，默认QVGA/RGB565/单帧缓冲。 */
    ESP_GOTO_ON_ERROR(camera_device_open(&camera, bus, expander, &config), cleanup, TAG, "open");
    for (unsigned i = 0; i < 10; ++i) {
        ESP_GOTO_ON_ERROR(camera_device_get_frame(&camera, &frame), cleanup, TAG, "capture");
        ESP_LOGI(TAG, "frame=%u width=%u height=%u format=%d bytes=%u",i, (unsigned)frame->width, (unsigned)frame->height, (int)frame->format, (unsigned)frame->len);
        /* 在这里同步处理/复制帧；异步LCD DMA完成前不能return_frame。 */
        ESP_GOTO_ON_ERROR(camera_device_return_frame(&camera, &frame), cleanup, TAG, "return");
    }
cleanup:
    if (frame) ESP_ERROR_CHECK(camera_device_return_frame(&camera, &frame));
    /* 清理失败不可返回并丢失栈上上下文，示例采用fatal策略。 */
    ESP_ERROR_CHECK(camera_device_close(&camera));
    return ret;
}

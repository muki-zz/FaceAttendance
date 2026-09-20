#include <string.h>
#include "camera_device.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "i2c_bus.h"

// #if !defined(CONFIG_SCCB_HARDWARE_I2C_DRIVER_LEGACY) || !CONFIG_SCCB_HARDWARE_I2C_DRIVER_LEGACY
// #error "Select Camera configuration -> I2C driver selection for SCCB -> Legacy I2C driver"
// #endif
// #if !defined(CONFIG_GC0308_SUPPORT) || !CONFIG_GC0308_SUPPORT
// #error "Enable Camera configuration -> Support GC0308 VGA"
// #endif

static const char *TAG = "camera_device";
static camera_device_t *owner; /* 单管理任务约定；不是跨任务互斥锁。 */

void camera_device_default_config(camera_device_config_t *config)
{
    if (!config) return;
    *config = (camera_device_config_t) {
        .pixel_format = PIXFORMAT_RGB565, .frame_size = FRAMESIZE_QVGA,
        .xclk_hz = 20000000, .pwdn_active_high = true,
    };
}

static esp_err_t ready(camera_device_t *dev)
{
    ESP_RETURN_ON_FALSE(dev && owner == dev && dev->initialized && !dev->fault,
                        ESP_ERR_INVALID_STATE, TAG, "camera not ready");
    return ESP_OK;
}

esp_err_t camera_device_open(camera_device_t *dev, i2c_bus_handle_t bus,
                             pca9557_handle_t expander,
                             const camera_device_config_t *config)
{
    ESP_RETURN_ON_FALSE(dev && bus && expander && config, ESP_ERR_INVALID_ARG, TAG, "argument");

    ESP_RETURN_ON_FALSE(!owner && !dev->expander && !dev->initialized && !dev->borrowed && !esp_camera_sensor_get(), ESP_ERR_INVALID_STATE, TAG, "camera already owned");
    
    ESP_RETURN_ON_FALSE(config->pixel_format == PIXFORMAT_RGB565 &&
                        (config->frame_size == FRAMESIZE_QQVGA ||config->frame_size == FRAMESIZE_QVGA 
                        || config->frame_size == FRAMESIZE_VGA) 
                        &&config->xclk_hz >= 10000000 && config->xclk_hz <= 24000000,
                        ESP_ERR_INVALID_ARG, TAG, "invalid format/size/clock");
    
    //获取总的堆空间大小
    ESP_RETURN_ON_FALSE(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0, ESP_ERR_INVALID_STATE, TAG, "enable PSRAM first");

    owner = dev;
    dev->expander = expander;
    dev->pwdn_active_high = config->pwdn_active_high;
    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_ERROR(board_camera_wake(expander, config->pwdn_active_high), fail, TAG, "wake sensor");     //唤醒摄像头

    camera_config_t driver_config;
    board_camera_default_config(&driver_config);        //获取默认摄像头的配置参数

    driver_config.pixel_format = config->pixel_format;
    driver_config.frame_size = config->frame_size;
    driver_config.sccb_i2c_port = bus->port;
    driver_config.xclk_freq_hz = config->xclk_hz;
    /* pin_sccb_sda=-1是复用模式，组件不会删除借用的Legacy I2C端口。 */
    ESP_GOTO_ON_ERROR(esp_camera_init(&driver_config), fail, TAG, "component init/probe");
    dev->initialized = true;

    //获取摄像头的句柄
    sensor_t *sensor = esp_camera_sensor_get();
    ESP_GOTO_ON_FALSE(sensor, ESP_FAIL, fail, TAG, "sensor missing after init");
    ESP_GOTO_ON_FALSE(sensor->id.PID == GC0308_PID && sensor->slv_addr == GC0308_SCCB_ADDR,
                      ESP_ERR_NOT_SUPPORTED, fail, TAG, "expected GC0308 at 0x21");
    /* 官方init未检查set_pixformat返回值，这里再确认同一格式设置成功。 */
    ESP_GOTO_ON_FALSE(sensor->set_pixformat && sensor->set_pixformat(sensor, PIXFORMAT_RGB565) == 0,
                      ESP_FAIL, fail, TAG, "RGB565 register configuration failed");
    /* 不让组件的分辨率静默降级被当成请求成功。 */
    ESP_GOTO_ON_FALSE(sensor->status.framesize == config->frame_size &&
                      sensor->pixformat == config->pixel_format,
                      ESP_ERR_NOT_SUPPORTED, fail, TAG, "sensor cannot apply requested format/size");
    ESP_LOGI(TAG, "PID=0x%04x SCCB=0x%02x; single framebuffer in PSRAM",
             (unsigned)sensor->id.PID, (unsigned)sensor->slv_addr);
    return ESP_OK;
fail:
    /* 已核对的官方init失败路径负责释放其内部状态。成功后失败则由close释放。 */
    ESP_ERROR_CHECK_WITHOUT_ABORT(camera_device_close(dev));
    return ret;
}

esp_err_t camera_device_get_frame(camera_device_t *dev, camera_fb_t **fb)
{
    ESP_RETURN_ON_FALSE(fb, ESP_ERR_INVALID_ARG, TAG, "null output");
    *fb = NULL;
    ESP_RETURN_ON_ERROR(ready(dev), TAG, "not ready");
    ESP_RETURN_ON_FALSE(!dev->borrowed, ESP_ERR_INVALID_STATE, TAG, "return previous frame first");
    camera_fb_t *frame = esp_camera_fb_get();
    ESP_RETURN_ON_FALSE(frame, ESP_ERR_TIMEOUT, TAG, "no frame; check PCLK/VSYNC/power");
    if (!frame->buf || !frame->width || !frame->height ||
        frame->width > 640 || frame->height > 480 || frame->format != PIXFORMAT_RGB565 ||
        frame->len != frame->width * frame->height * sizeof(uint16_t)) {
        esp_camera_fb_return(frame);
        return ESP_ERR_INVALID_SIZE;
    }
    dev->borrowed = frame;
    *fb = frame;
    return ESP_OK;
}

esp_err_t camera_device_return_frame(camera_device_t *dev, camera_fb_t **fb)
{
    ESP_RETURN_ON_FALSE(dev && fb && *fb, ESP_ERR_INVALID_ARG, TAG, "invalid frame");
    ESP_RETURN_ON_FALSE(owner == dev && dev->initialized && dev->borrowed == *fb,
                        ESP_ERR_INVALID_STATE, TAG, "frame not owned by this camera");
    esp_camera_fb_return(*fb);
    dev->borrowed = NULL;
    *fb = NULL;
    return ESP_OK;
}

esp_err_t camera_device_set_orientation(camera_device_t *dev, bool mirror, bool flip)
{
    ESP_RETURN_ON_ERROR(ready(dev), TAG, "not ready");
    ESP_RETURN_ON_FALSE(!dev->borrowed, ESP_ERR_INVALID_STATE, TAG, "frame borrowed");
    sensor_t *sensor = esp_camera_sensor_get();
    ESP_RETURN_ON_FALSE(sensor && sensor->set_hmirror && sensor->set_vflip,
                        ESP_ERR_NOT_SUPPORTED, TAG, "orientation not supported");
    if (sensor->set_hmirror(sensor, mirror) != 0 || sensor->set_vflip(sensor, flip) != 0) {
        dev->fault = true; /* 配置可能部分完成，close/open恢复一致状态。 */
        return ESP_FAIL;
    }
    /* 寄存器变化后的管线旧帧，丢弃两帧；实际曝光稳定时间随传感器而异。 */
    for (unsigned i = 0; i < 2; ++i) {
        camera_fb_t *frame = esp_camera_fb_get();
        ESP_RETURN_ON_FALSE(frame, ESP_ERR_TIMEOUT, TAG, "drain old frame");
        esp_camera_fb_return(frame);
    }
    return ESP_OK;
}

esp_err_t camera_device_get_id(camera_device_t *dev, sensor_id_t *id, uint8_t *address)
{
    ESP_RETURN_ON_FALSE(id && address, ESP_ERR_INVALID_ARG, TAG, "argument");
    ESP_RETURN_ON_ERROR(ready(dev), TAG, "not ready");
    sensor_t *sensor = esp_camera_sensor_get();
    ESP_RETURN_ON_FALSE(sensor, ESP_ERR_INVALID_STATE, TAG, "sensor unavailable");
    *id = sensor->id;
    *address = sensor->slv_addr;
    return ESP_OK;
}

esp_err_t camera_device_set_colorbar(camera_device_t *dev, bool enabled)
{
    ESP_RETURN_ON_ERROR(ready(dev), TAG, "not ready");
    ESP_RETURN_ON_FALSE(!dev->borrowed, ESP_ERR_INVALID_STATE, TAG, "return frame first");
    sensor_t *sensor = esp_camera_sensor_get();
    ESP_RETURN_ON_FALSE(sensor && sensor->set_colorbar, ESP_ERR_NOT_SUPPORTED, TAG, "no colorbar control");
    if (sensor->set_colorbar(sensor, enabled) != 0) {
        dev->fault = true;
        return ESP_FAIL;
    }
    /* 传感器与DMA可能仍持有旧帧，丢弃过渡帧。 */
    for (unsigned i = 0; i < 2; ++i) {
        camera_fb_t *frame = esp_camera_fb_get();
        ESP_RETURN_ON_FALSE(frame, ESP_ERR_TIMEOUT, TAG, "drain colorbar transition");
        esp_camera_fb_return(frame);
    }
    return ESP_OK;
}

esp_err_t camera_device_capture_to_file(camera_device_t *dev, FILE *file, size_t *bytes)
{
    ESP_RETURN_ON_FALSE(file && bytes, ESP_ERR_INVALID_ARG, TAG, "argument");
    *bytes = 0;
    camera_fb_t *frame = NULL;
    ESP_RETURN_ON_ERROR(camera_device_get_frame(dev, &frame), TAG, "capture");
    *bytes = fwrite(frame->buf, 1, frame->len, file);
    esp_err_t ret = *bytes == frame->len ? ESP_OK : ESP_FAIL;
    if (fflush(file) != 0) ret = ESP_FAIL;
    esp_err_t release = camera_device_return_frame(dev, &frame);
    return ret == ESP_OK ? release : ret;
}

esp_err_t camera_device_close(camera_device_t *dev)
{
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_INVALID_ARG, TAG, "null device");
    if (!dev->expander && !dev->initialized) return ESP_OK;
    ESP_RETURN_ON_FALSE(owner == dev && !dev->borrowed,
                        ESP_ERR_INVALID_STATE, TAG, "wrong owner or outstanding frame");
    if (dev->initialized) {
        ESP_RETURN_ON_ERROR(esp_camera_deinit(), TAG, "deinit failed; retain context");
        dev->initialized = false;
    }
    ESP_RETURN_ON_ERROR(board_camera_sleep(dev->expander, dev->pwdn_active_high), TAG, "PWDN failed; retry close");
    memset(dev, 0, sizeof(*dev));
    owner = NULL;
    return ESP_OK;
}

#pragma once
#include <stdio.h>
#include "board_camera.h"


typedef struct {
    pixformat_t pixel_format; /* GC0308版本限定RGB565，不支持硬件JPEG */
    framesize_t frame_size;
    int xclk_hz;              /* 默认20MHz；必须匹配实际模块 */
    bool pwdn_active_high;    /* 本板GC0308为true */
} camera_device_config_t;

typedef struct {
    pca9557_handle_t expander; /* 借用，生命周期必须长于摄像头 */
    camera_fb_t *borrowed;     /* 内部记录，不允许应用直接修改 */
    bool initialized, fault, pwdn_active_high;
} camera_device_t;

/* 首次对象={0}；全局单摄像头、所有调用由同一任务串行管理。
 * 不得混用直接esp_camera_*调用；共享I2C须通过既有i2c_bus_get初始化。
 * 本层只查询传入bus的端口与配置，不调用i2c_bus_get或install。
 * open失败也应close回收可能保留的PWDN控制状态。 */
void camera_device_default_config(camera_device_config_t *config);

esp_err_t camera_device_open(camera_device_t *dev, i2c_bus_handle_t bus,
                             pca9557_handle_t expander,
                             const camera_device_config_t *config);

/* fb是借用的组件帧；不可free。一次最多借一帧，SDK内部等待约4秒。
 * 接口不支持用户指定毫秒超时；NULL帧按ESP_ERR_TIMEOUT报告。 */

esp_err_t camera_device_get_frame(camera_device_t *dev, camera_fb_t **fb);

esp_err_t camera_device_return_frame(camera_device_t *dev, camera_fb_t **fb);

/* 无借出帧时才能改镜像/翻转；不是90度旋转。部分寄存器写失败后进入fault。 */
esp_err_t camera_device_set_orientation(camera_device_t *dev, bool hmirror, bool vflip);
esp_err_t camera_device_set_colorbar(camera_device_t *dev, bool enabled);
esp_err_t camera_device_get_id(camera_device_t *dev, sensor_id_t *id, uint8_t *address);
/* 向已打开FILE写一帧，保证归还帧。不挂载、不打开/关闭文件。
 * RGB565只是裸像素，无文件头，不能直接命名成.jpg/.bmp。
 * 数据字节序保留组件输出；送LCD或转换文件前需核对目标要求。 */
esp_err_t camera_device_capture_to_file(camera_device_t *dev, FILE *file, size_t *bytes);
/* 存在借出帧则拒绝关闭；先停采集使用者、归还帧，再close。
 * 关闭组件后置PWDN，不删除共享I2C或PCA句柄。 */
esp_err_t camera_device_close(camera_device_t *dev);

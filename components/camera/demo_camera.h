#pragma once
#include "pca9557_io.h"

/* 使用板级已有共享PCA句柄，探测并采集十帧，仅打印帧信息。
 * 未定义app_main、不会自动执行。expander不得在执行中释放。 */
esp_err_t demo_camera_capture(i2c_bus_handle_t bus, pca9557_handle_t expander);

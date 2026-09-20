#pragma once
#include <stdbool.h>
#include "esp_camera.h"
#include "pca9557.h"

/* J6接线，SCCB复用已初始化I2C0；不会安装/删除I2C。 */
void board_camera_default_config(camera_config_t *config);
/* 借用板级共享PCA9557，IO2为PWDN；true表示高电平休眠。
 * active_high必须与实际摄像头模块匹配，连接器型号不能确定极性。 */
esp_err_t board_camera_wake(pca9557_handle_t expander, bool active_high);
esp_err_t board_camera_sleep(pca9557_handle_t expander, bool active_high);

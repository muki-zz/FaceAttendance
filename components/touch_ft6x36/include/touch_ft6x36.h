#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

typedef struct touch_ft6x36 *touch_ft6x36_handle_t;
typedef struct {
    i2c_master_bus_handle_t bus; // 借用已初始化的新版 I2C Master 总线
    uint32_t scl_speed_hz;       // FT6x36设备通信频率
    uint16_t raw_width;          // 未变换坐标范围：0..raw_width-1
    uint16_t raw_height;
    bool swap_xy;
    bool mirror_x;               // 在交换坐标之后，按输出宽度镜像
    bool mirror_y;
} touch_ft6x36_config_t;

typedef struct {
    uint16_t x, y;
    uint16_t raw_x, raw_y;
    uint8_t id;                  // 控制器触点 ID，供多点追踪
} touch_ft6x36_point_t;

typedef struct {
    uint8_t count;               // 0 表示没有按下；最多 2 个有效触点
    touch_ft6x36_point_t points[2];
} touch_ft6x36_sample_t;

// 本板 CTP_RST 接系统 RESET，初始化不驱动复位线；INT=GPIO10 未使用。
esp_err_t touch_ft6x36_create(const touch_ft6x36_config_t *config, touch_ft6x36_handle_t *out);
// 每次从硬件读取；失败时 sample 清零，调用者必须检查返回码。
esp_err_t touch_ft6x36_read(touch_ft6x36_handle_t touch, touch_ft6x36_sample_t *sample);
esp_err_t touch_ft6x36_set_direction(touch_ft6x36_handle_t touch, bool swap_xy, bool mirror_x, bool mirror_y);
// 设置交换坐标后的输出轴镜像，保留 swap_xy 和原始宽高。
// 参数为绝对设置，重复调用不会反复翻转；下次读取生效，任务上下文调用。
esp_err_t touch_ft6x36_set_mirror(touch_ft6x36_handle_t touch, bool mirror_x, bool mirror_y);
// 先停止所有读取任务；不得与 read/set_direction 并发删除。
esp_err_t touch_ft6x36_delete(touch_ft6x36_handle_t touch);

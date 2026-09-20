#pragma once
#include "audio_i2c_bus.h"
#include "audio_clock.h"
typedef struct {
    audio_ctrl_bus_t *bus; // 借用，不拥有总线。
} raw_es8311_t;
// 查表预检不操作硬件；init/set_clock 不接受任意近似频率。
esp_err_t raw_es8311_validate_clock(const audio_clock_config_t *clock);
// 仅限初始化阶段或静音、停止数据提交的受控阶段调用，不用于播放中热切换。
esp_err_t raw_es8311_set_clock(raw_es8311_t *dev,
                               const audio_clock_config_t *clock);
esp_err_t raw_es8311_init(raw_es8311_t *dev, audio_ctrl_bus_t *bus,
                          const audio_clock_config_t *clock);
esp_err_t raw_es8311_volume(raw_es8311_t *dev, unsigned percent);
esp_err_t raw_es8311_mute(raw_es8311_t *dev, bool mute);

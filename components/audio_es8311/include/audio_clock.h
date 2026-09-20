#pragma once
#include <stdint.h>

typedef struct {
    uint32_t mclk_hz;         // ESP32 实际要输出的主时钟，Hz。
    uint32_t sample_rate_hz;  // 音频采样率，Hz。
} audio_clock_config_t;

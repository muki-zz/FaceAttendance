#include "esp_check.h"
#include "board_pa.h"
esp_err_t board_pa_set(pca9557_handle_t expander, bool on)
{
    // 只改 IO1，IO0 的 LCD_CS 和其他输出全部保留。
    return pca9557_set_level(expander, 1, on);
}

esp_err_t board_pa_prepare(pca9557_handle_t expander)
{
    // 先将输出锁存值写低，再把 IO1 切成输出，避免瞬间打开功放。
    return pca9557_config_output(expander, 1, false);
}

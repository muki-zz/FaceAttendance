#include "esp_check.h"
#include "es8311_raw.h"
#include "freertos/task.h"

enum { ES_ADDR = 0x18, ES_RESET = 0x00, ES_DAC_CTRL = 0x31,
       ES_DAC_VOLUME = 0x32 };

typedef struct {
    uint32_t mclk_hz, sample_rate_hz;
    uint8_t pre_div, pre_multi;
    uint8_t adc_div, dac_div, fs_mode;
    uint8_t lrck_h, lrck_l, bclk_div;
    uint8_t adc_osr, dac_osr;
} es8311_clock_coeff_t;

static const es8311_clock_coeff_t clock_table[] = {
    /* MCLK    Fs     pre mult adc dac fs  LRH   LRL   BCK  ADCOSR DACOSR */
    {12288000, 16000, 3,  0,   1,  1,  0,  0x00, 0xff, 4,  0x10, 0x10},
    {18432000, 16000, 3,  1,   3,  3,  0,  0x02, 0xff, 12, 0x10, 0x10},
    {16384000, 16000, 4,  0,   1,  1,  0,  0x00, 0xff, 4,  0x10, 0x10},
    { 8192000, 16000, 2,  0,   1,  1,  0,  0x00, 0xff, 4,  0x10, 0x10},
    { 6144000, 16000, 3,  1,   1,  1,  0,  0x00, 0xff, 4,  0x10, 0x10},
    { 4096000, 16000, 1,  0,   1,  1,  0,  0x00, 0xff, 4,  0x10, 0x20},
};

static const es8311_clock_coeff_t *find_clock(const audio_clock_config_t *clock)
{
    if (!clock) return NULL;
    for (size_t i = 0; i < sizeof(clock_table) / sizeof(clock_table[0]); ++i) {
        if (clock_table[i].mclk_hz == clock->mclk_hz &&
            clock_table[i].sample_rate_hz == clock->sample_rate_hz)
            return &clock_table[i];
    }
    return NULL;
}

esp_err_t raw_es8311_validate_clock(const audio_clock_config_t *clock)
{
    ESP_RETURN_ON_FALSE(clock != NULL, ESP_ERR_INVALID_ARG, "es8311_raw", "argument, state or transfer check failed");
    return find_clock(clock) ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t wr(raw_es8311_t *d, uint8_t reg, uint8_t value)
{
    ESP_RETURN_ON_FALSE(d && d->bus, ESP_ERR_INVALID_ARG, "es8311_raw", "argument, state or transfer check failed");
    return audio_reg_write(d->bus, ES_ADDR, reg, value);
}

esp_err_t raw_es8311_set_clock(raw_es8311_t *d,
                               const audio_clock_config_t *clock)
{
    ESP_RETURN_ON_FALSE(d && d->bus && clock, ESP_ERR_INVALID_ARG, "es8311_raw", "argument, state or transfer check failed");
    const es8311_clock_coeff_t *c = find_clock(clock);
    ESP_RETURN_ON_FALSE(c != NULL, ESP_ERR_NOT_SUPPORTED, "es8311_raw", "argument, state or transfer check failed"); // 未命中：不写任何寄存器。

    // 以掩码更新保留无关字段；以下编码对应官方驱动。
    esp_err_t e = audio_reg_update(d->bus, ES_ADDR, 0x02, 0xf8,
        (uint8_t)(((c->pre_div - 1) << 5) | (c->pre_multi << 3)));
    ESP_RETURN_ON_ERROR(e, "es8311_raw", "operation failed");
    e = wr(d, 0x03, (uint8_t)((c->fs_mode << 6) | c->adc_osr));
    ESP_RETURN_ON_ERROR(e, "es8311_raw", "operation failed");
    e = wr(d, 0x04, c->dac_osr);
    ESP_RETURN_ON_ERROR(e, "es8311_raw", "operation failed");
    e = wr(d, 0x05, (uint8_t)(((c->adc_div - 1) << 4) | (c->dac_div - 1)));
    ESP_RETURN_ON_ERROR(e, "es8311_raw", "operation failed");
    uint8_t bclk_code = c->bclk_div < 19 ? c->bclk_div - 1 : c->bclk_div;
    e = audio_reg_update(d->bus, ES_ADDR, 0x06, 0x1f, bclk_code);
    ESP_RETURN_ON_ERROR(e, "es8311_raw", "operation failed");
    e = audio_reg_update(d->bus, ES_ADDR, 0x07, 0x3f, c->lrck_h);
    ESP_RETURN_ON_ERROR(e, "es8311_raw", "operation failed");
    return wr(d, 0x08, c->lrck_l);
}

esp_err_t raw_es8311_mute(raw_es8311_t *d, bool mute)
{
    ESP_RETURN_ON_FALSE(d && d->bus, ESP_ERR_INVALID_ARG, "es8311_raw", "argument, state or transfer check failed");
    return audio_reg_update(d->bus, ES_ADDR, ES_DAC_CTRL,
                             0x60, mute ? 0x60 : 0x00);
}

esp_err_t raw_es8311_volume(raw_es8311_t *d, unsigned percent)
{
    ESP_RETURN_ON_FALSE(percent <= 100, ESP_ERR_INVALID_ARG, "es8311_raw", "argument, state or transfer check failed");
    // 延用参考驱动的逻辑百分比映射，不是线性声压百分比。
    uint8_t value = percent ? (uint8_t)(percent * 256 / 100 - 1) : 0;
    return wr(d, ES_DAC_VOLUME, value);
}

esp_err_t raw_es8311_init(raw_es8311_t *d, audio_ctrl_bus_t *bus,
                          const audio_clock_config_t *clock)
{
    ESP_RETURN_ON_FALSE(d && bus, ESP_ERR_INVALID_ARG, "es8311_raw", "argument, state or transfer check failed");
    esp_err_t e = raw_es8311_validate_clock(clock);
    ESP_RETURN_ON_ERROR(e, "es8311_raw", "operation failed"); // 先查表，再复位芯片。
    d->bus = bus;
    // 前置条件：功放关闭、I²S 已提供连续时钟。
    e = wr(d, ES_RESET, 0x1f);
    ESP_RETURN_ON_ERROR(e, "es8311_raw", "operation failed");
    vTaskDelay(pdMS_TO_TICKS(20));
    e = wr(d, ES_RESET, 0x00);
    ESP_RETURN_ON_ERROR(e, "es8311_raw", "operation failed");
    e = wr(d, ES_RESET, 0x80); // 上电，从机模式。
    ESP_RETURN_ON_ERROR(e, "es8311_raw", "operation failed");
    e = wr(d, 0x01, 0x3f); // MCLK 引脚源、不反相、时钟使能。
    ESP_RETURN_ON_ERROR(e, "es8311_raw", "operation failed");
    e = audio_reg_update(bus, ES_ADDR, 0x06, 0x20, 0x00); // BCLK 不反相。
    ESP_RETURN_ON_ERROR(e, "es8311_raw", "operation failed");
    e = raw_es8311_set_clock(d, clock);
    ESP_RETURN_ON_ERROR(e, "es8311_raw", "operation failed");
    // 格式/模拟路径初始化与时钟表分开。
    static const uint8_t seq[][2] = {
        {0x09, 0x0c},
        {0x0a, 0x0c}, 
        {0x13, 0x10}, 
        {0x1b, 0x0a},
        {0x1c, 0x6a},
        {0x0e, 0x02},
        {0x12, 0x00},
        {0x14, 0x1a},
        {0x0d, 0x01},
        {0x15, 0x40},
        {0x37, 0x08},
        {0x45, 0x00},
        {0x31, 0x60},
        {0x32, 0x00},
    };
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); ++i) {
        e = wr(d, seq[i][0], seq[i][1]);
        ESP_RETURN_ON_ERROR(e, "es8311_raw", "operation failed"); // 不忽略任何初始化写失败。
    }
    vTaskDelay(pdMS_TO_TICKS(50)); // 板级保守稳定时间，需上板调优。
    return ESP_OK;
}

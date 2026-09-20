#include <stdint.h>

#include "esp_check.h"
#include "esp_log.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"

#include "audio_i2s_bus.h"

static const char *TAG = "audio_i2s_bus";

esp_err_t audio_tx_validate_clock(const audio_clock_config_t *clock)
{
    ESP_RETURN_ON_FALSE(clock != NULL,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "invalid clock config");
    ESP_RETURN_ON_FALSE(clock->sample_rate_hz == 16000 &&
                        (clock->mclk_hz == 4096000 ||
                         clock->mclk_hz == 6144000),
                        ESP_ERR_NOT_SUPPORTED,
                        TAG,
                        "only 16kHz with 4.096/6.144MHz is supported");
    return ESP_OK;
}

static esp_err_t audio_i2s_bus_release(audio_i2s_bus_t *bus)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t first = ESP_OK;

    if (bus->rx && bus->rx_enabled) {
        esp_err_t ret = i2s_channel_disable(bus->rx);
        if (ret == ESP_OK) {
            bus->rx_enabled = false;
        }
        else if (first == ESP_OK) {
            first = ret;
        }
    }

    if (bus->tx && bus->tx_enabled) {
        esp_err_t ret = i2s_channel_disable(bus->tx);
        if (ret == ESP_OK) {
            bus->tx_enabled = false;
        }
        else if (first == ESP_OK) {
            first = ret;
        }
    }

    if (bus->rx && !bus->rx_enabled) {
        esp_err_t ret = i2s_del_channel(bus->rx);
        if (ret == ESP_OK) {
            bus->rx = NULL;
        }
        else if (first == ESP_OK) {
            first = ret;
        }
    }

    if (bus->tx && !bus->tx_enabled) {
        esp_err_t ret = i2s_del_channel(bus->tx);
        if (ret == ESP_OK) {
            bus->tx = NULL;
        }
        else if (first == ESP_OK) {
            first = ret;
        }
    }

    if (!bus->rx && !bus->tx) {
        *bus = (audio_i2s_bus_t){0};
    }

    return first;
}

static void report_open_cleanup(audio_i2s_bus_t *bus)
{
    esp_err_t cleanup = audio_i2s_bus_release(bus);
    if (cleanup != ESP_OK) {
        ESP_LOGE(TAG, "I2S cleanup after open failure failed: %s",
                 esp_err_to_name(cleanup));
    }
}

esp_err_t audio_i2s_bus_open(audio_i2s_bus_t *bus,
                             const audio_clock_config_t *clock)
{
    ESP_RETURN_ON_FALSE(bus && clock,
                        ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(bus->tx == NULL && bus->rx == NULL,
                        ESP_ERR_INVALID_STATE, TAG,
                        "I2S bus already opened");
    ESP_RETURN_ON_ERROR(audio_tx_validate_clock(clock),
                        TAG, "unsupported audio clock");

    *bus = (audio_i2s_bus_t){0};

    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 256;
    chan_cfg.auto_clear = true;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &bus->tx, &bus->rx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel(full-duplex) failed: %s",
                 esp_err_to_name(ret));
        report_open_cleanup(bus);
        return ret;
    }

    i2s_std_config_t tx_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(clock->sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_38,
            .bclk = GPIO_NUM_14,
            .ws = GPIO_NUM_13,
            .dout = GPIO_NUM_45,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    tx_cfg.clk_cfg.mclk_multiple =
        (clock->mclk_hz == 6144000) ?
        I2S_MCLK_MULTIPLE_384 : I2S_MCLK_MULTIPLE_256;

    i2s_std_config_t rx_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(clock->sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = GPIO_NUM_14,
            .ws = GPIO_NUM_13,
            .dout = I2S_GPIO_UNUSED,
            .din = GPIO_NUM_12,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    rx_cfg.clk_cfg.mclk_multiple = tx_cfg.clk_cfg.mclk_multiple;

    ret = i2s_channel_init_std_mode(bus->tx, &tx_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TX init failed: %s", esp_err_to_name(ret));
        report_open_cleanup(bus);
        return ret;
    }

    ret = i2s_channel_init_std_mode(bus->rx, &rx_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RX init failed: %s", esp_err_to_name(ret));
        report_open_cleanup(bus);
        return ret;
    }

    int16_t silence[512] = {0};
    size_t loaded = 0;

    ret = i2s_channel_preload_data(bus->tx,
                                   silence,
                                   sizeof(silence),
                                   &loaded);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TX preload failed: %s", esp_err_to_name(ret));
        report_open_cleanup(bus);
        return ret;
    }

    ret = i2s_channel_enable(bus->tx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TX enable failed: %s", esp_err_to_name(ret));
        report_open_cleanup(bus);
        return ret;
    }
    bus->tx_enabled = true;

    ret = i2s_channel_enable(bus->rx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RX enable failed: %s", esp_err_to_name(ret));
        report_open_cleanup(bus);
        return ret;
    }
    bus->rx_enabled = true;

    ESP_LOGI(TAG,
             "I2S0 full-duplex ready: Fs=%lu Hz, MCLK=%lu Hz, "
             "BCLK=GPIO14, WS=GPIO13, DIN=GPIO12, DOUT=GPIO45",
             (unsigned long)clock->sample_rate_hz,
             (unsigned long)clock->mclk_hz);

    return ESP_OK;
}

esp_err_t audio_i2s_bus_close(audio_i2s_bus_t *bus)
{
    ESP_RETURN_ON_FALSE(bus != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    return audio_i2s_bus_release(bus);
}

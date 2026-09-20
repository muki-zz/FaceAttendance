#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "driver/i2s_std.h"

#include "audio_clock.h"

typedef struct{
    i2s_chan_handle_t tx;
    i2s_chan_handle_t rx;

    bool tx_enabled;
    bool rx_enabled;
}audio_i2s_bus_t;

esp_err_t audio_tx_validate_clock(const audio_clock_config_t *clock);

esp_err_t audio_i2s_bus_open(audio_i2s_bus_t *bus, const audio_clock_config_t *clock);

esp_err_t audio_i2s_bus_close(audio_i2s_bus_t *bus);
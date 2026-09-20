#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/i2s_std.h"

#include "audio_i2s_bus.h"
#include "audio_clock.h"
#include "es8311_raw.h"
#include "i2c_bus.h"
#include "pca9557_io.h"

typedef enum {
    AO_EMPTY = 0,
    AO_READY,
    AO_PLAYING,
    AO_STOPPED,
    AO_FAULT,
} audio_out_state_t;

typedef struct {
    /*
     * The I2C adapter is stored by value so the codec never keeps a pointer
     * to a temporary stack object owned by app_main().
     */
    audio_ctrl_bus_t             bus;
    pca9557_handle_t             expander;
    audio_clock_config_t         clock;
    unsigned                     volume;
    bool                         muted;
    audio_out_state_t            state;

    i2s_chan_handle_t            tx;
    raw_es8311_t                 codec;
    int16_t                      stereo[512];
} audio_out_t;

esp_err_t audio_out_open(audio_out_t *o,
                         i2c_bus_handle_t i2c,
                         pca9557_handle_t expander,
                         i2s_chan_handle_t tx,
                         const audio_clock_config_t *clock);
esp_err_t audio_out_start(audio_out_t *o);
esp_err_t audio_out_set_volume(audio_out_t *o, unsigned percent);
esp_err_t audio_out_set_mute(audio_out_t *o, bool mute);
esp_err_t audio_out_write(audio_out_t *o, const int16_t *pcm,
                          size_t count, size_t *consumed);
esp_err_t audio_out_stop(audio_out_t *o);
esp_err_t audio_out_close(audio_out_t *o);

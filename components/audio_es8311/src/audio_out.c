#include "esp_check.h"
#include "esp_log.h"

#include "audio_out.h"
#include "board_pa.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "audio_out";

static void report_cleanup(esp_err_t e)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(e);
}

static esp_err_t fail(audio_out_t *o, esp_err_t primary)
{
    if (o == NULL) {
        return primary;
    }

    o->state = AO_FAULT;

    if (o->expander) {
        report_cleanup(board_pa_set(o->expander, false));
    }

    if (o->codec.bus) {
        report_cleanup(raw_es8311_mute(&o->codec, true));
    }

    return primary;
}

esp_err_t audio_out_open(audio_out_t *o,
                         i2c_bus_handle_t i2c,
                         pca9557_handle_t expander,
                         i2s_chan_handle_t tx,
                         const audio_clock_config_t *clock)
{
    ESP_RETURN_ON_FALSE(o && i2c && expander && tx && clock,
                        ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(o->state == AO_EMPTY && !o->tx,
                        ESP_ERR_INVALID_STATE, TAG, "already opened");

    esp_err_t e = audio_tx_validate_clock(clock);
    ESP_RETURN_ON_ERROR(e, TAG, "unsupported I2S clock");

    e = raw_es8311_validate_clock(clock);
    ESP_RETURN_ON_ERROR(e, TAG, "unsupported ES8311 clock");

    *o = (audio_out_t){
        .bus = {
            .handle = i2c,
        },
        .expander = expander,
        .clock = *clock,
        .volume = 60,
        .muted = true,
        .state = AO_EMPTY,
        .tx = tx,
    };

    o->codec.bus = &o->bus;

    e = board_pa_prepare(o->expander);
    ESP_RETURN_ON_FALSE(e == ESP_OK, fail(o, e), TAG,
                        "board PA prepare failed");

    e = raw_es8311_init(&o->codec, &o->bus, &o->clock);
    ESP_RETURN_ON_FALSE(e == ESP_OK, fail(o, e), TAG,
                        "ES8311 init failed");

    e = raw_es8311_volume(&o->codec, o->volume);
    ESP_RETURN_ON_FALSE(e == ESP_OK, fail(o, e), TAG,
                        "ES8311 volume failed");

    o->state = AO_READY;
    return ESP_OK;
}

esp_err_t audio_out_start(audio_out_t *o)
{
    ESP_RETURN_ON_FALSE(o && o->state == AO_READY,
                        ESP_ERR_INVALID_STATE, TAG, "not ready");

    esp_err_t e = board_pa_set(o->expander, true);
    ESP_RETURN_ON_FALSE(e == ESP_OK, fail(o, e), TAG,
                        "PA enable failed");

    vTaskDelay(pdMS_TO_TICKS(30));

    e = raw_es8311_mute(&o->codec, o->muted || o->volume == 0);
    ESP_RETURN_ON_FALSE(e == ESP_OK, fail(o, e), TAG,
                        "mute failed");

    o->state = AO_PLAYING;
    return ESP_OK;
}

esp_err_t audio_out_set_volume(audio_out_t *o, unsigned percent)
{
    ESP_RETURN_ON_FALSE(o && percent <= 100,
                        ESP_ERR_INVALID_ARG, TAG, "invalid volume");
    ESP_RETURN_ON_FALSE(o->state == AO_READY || o->state == AO_PLAYING,
                        ESP_ERR_INVALID_STATE, TAG, "invalid state");

    esp_err_t e = raw_es8311_volume(&o->codec, percent);
    ESP_RETURN_ON_FALSE(e == ESP_OK, fail(o, e), TAG,
                        "volume write failed");

    o->volume = percent;

    e = raw_es8311_mute(&o->codec,
                        o->state != AO_PLAYING || o->muted || percent == 0);
    if (e != ESP_OK) {
        return fail(o, e);
    }

    return ESP_OK;
}

esp_err_t audio_out_set_mute(audio_out_t *o, bool mute)
{
    ESP_RETURN_ON_FALSE(o &&
                        (o->state == AO_READY || o->state == AO_PLAYING),
                        ESP_ERR_INVALID_STATE, TAG, "invalid state");

    esp_err_t e = raw_es8311_mute(&o->codec,
                                  o->state != AO_PLAYING ||
                                  mute ||
                                  o->volume == 0);
    ESP_RETURN_ON_FALSE(e == ESP_OK, fail(o, e), TAG,
                        "mute write failed");

    o->muted = mute;
    return ESP_OK;
}

esp_err_t audio_out_write(audio_out_t *o,
                          const int16_t *pcm,
                          size_t count,
                          size_t *consumed)
{
    ESP_RETURN_ON_FALSE(consumed != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "consumed is NULL");

    *consumed = 0;

    ESP_RETURN_ON_FALSE(o && (pcm || count == 0),
                        ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(o->state == AO_PLAYING,
                        ESP_ERR_INVALID_STATE, TAG,
                        "speaker is not playing");

    if (count == 0) {
        return ESP_OK;
    }

    while (*consumed < count) {
        size_t n = count - *consumed;
        if (n > 256) {
            n = 256;
        }

        for (size_t i = 0; i < n; ++i) {
            int16_t s = pcm[*consumed + i];
            o->stereo[2 * i] = s;
            o->stereo[2 * i + 1] = s;
        }

        const size_t bytes_expected = n * 2 * sizeof(int16_t);
        size_t written = 0;

        esp_err_t e = i2s_channel_write(o->tx,
                                        o->stereo,
                                        bytes_expected,
                                        &written,
                                        pdMS_TO_TICKS(1000));

        /*
         * Count only the mono samples that were actually accepted by I2S.
         * The old code added both "written samples" and n, which doubled
         * consumed and caused the caller to skip PCM data.
         */
        *consumed += written / (2 * sizeof(int16_t));

        if (e != ESP_OK) {
            ESP_LOGE(TAG, "I2S write failed: %s, written=%u/%u",
                     esp_err_to_name(e),
                     (unsigned)written,
                     (unsigned)bytes_expected);
            return fail(o, e);
        }

        if (written != bytes_expected) {
            ESP_LOGE(TAG, "I2S short write: written=%u/%u",
                     (unsigned)written,
                     (unsigned)bytes_expected);
            return fail(o, ESP_ERR_INVALID_SIZE);
        }
    }

    return ESP_OK;
}

esp_err_t audio_out_stop(audio_out_t *o)
{
    ESP_RETURN_ON_FALSE(o && o->bus.handle,
                        ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    esp_err_t first = raw_es8311_mute(&o->codec, true);
    esp_err_t e = board_pa_set(o->expander, false);

    if (first == ESP_OK) {
        first = e;
    }
    else if (e != ESP_OK) {
        report_cleanup(e);
    }

    o->state = (first == ESP_OK) ? AO_STOPPED : AO_FAULT;
    return first;
}

esp_err_t audio_out_close(audio_out_t *o)
{
    ESP_RETURN_ON_FALSE(o != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    if (!o->bus.handle && !o->tx) {
        return ESP_OK;
    }

    esp_err_t first = audio_out_stop(o);

    if (first == ESP_OK) {
        *o = (audio_out_t){0};
    }

    return first;
}

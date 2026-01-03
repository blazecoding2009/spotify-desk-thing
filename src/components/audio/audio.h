#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2s.h"
#include "esp_err.h"

typedef struct {
    i2s_port_t port;
    gpio_num_t mclk_pin;
    gpio_num_t bclk_pin;
    gpio_num_t lrclk_pin;
    gpio_num_t dout_pin;
    uint32_t sample_rate_hz;
} audio_i2s_config_t;

esp_err_t audio_init(const audio_i2s_config_t *config);
esp_err_t audio_play_beep(float frequency_hz, uint32_t duration_ms, float volume);
esp_err_t audio_play_wav_file(const char *path);
void audio_request_stop(void);
bool audio_is_playing(void);

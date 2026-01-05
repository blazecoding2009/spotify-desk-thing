#include "audio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "AUDIO"
#define AUDIO_DMA_BUFFER_FRAMES 256
#define AUDIO_WAV_CHUNK_BYTES (16 * 1024)

static audio_i2s_config_t s_cfg;
static bool s_initialized = false;
static volatile bool s_stop_requested = false;
static volatile bool s_is_playing = false;

static esp_err_t audio_configure_driver(const audio_i2s_config_t *config) {
    i2s_config_t i2s_conf = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = (int)config->sample_rate_hz,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = AUDIO_DMA_BUFFER_FRAMES,
        .use_apll = true,
        .tx_desc_auto_clear = true,
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    };

    ESP_RETURN_ON_ERROR(i2s_driver_install(config->port, &i2s_conf, 0, NULL), TAG, "i2s install failed");

    i2s_pin_config_t pin_config = {
        .mck_io_num = config->mclk_pin,
        .bck_io_num = config->bclk_pin,
        .ws_io_num = config->lrclk_pin,
        .data_out_num = config->dout_pin,
        .data_in_num = I2S_PIN_NO_CHANGE,
    };
    return i2s_set_pin(config->port, &pin_config);
}

esp_err_t audio_init(const audio_i2s_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_initialized) {
        return ESP_OK;
    }

    s_cfg = *config;
    ESP_RETURN_ON_ERROR(audio_configure_driver(config), TAG, "Driver config failed");
    ESP_RETURN_ON_ERROR(i2s_set_clk(config->port, config->sample_rate_hz, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO), TAG, "Set clk failed");
    s_initialized = true;
    return ESP_OK;
}

esp_err_t audio_play_beep(float frequency_hz, uint32_t duration_ms, float volume) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (frequency_hz <= 0) {
        frequency_hz = 880.0f;
    }
    if (volume <= 0.0f || volume > 1.0f) {
        volume = 0.3f;
    }

    size_t total_frames = (s_cfg.sample_rate_hz * duration_ms) / 1000;
    size_t buffer_samples = AUDIO_DMA_BUFFER_FRAMES;
    int16_t *buffer = heap_caps_malloc(buffer_samples * 2 * sizeof(int16_t), MALLOC_CAP_DMA);
    if (!buffer) {
        return ESP_ERR_NO_MEM;
    }

    float phase_increment = 2.0f * (float)M_PI * frequency_hz / (float)s_cfg.sample_rate_hz;
    float phase = 0.0f;
    size_t generated = 0;
    while (generated < total_frames) {
        size_t frames_now = (total_frames - generated) > buffer_samples ? buffer_samples : (total_frames - generated);
        for (size_t i = 0; i < frames_now; ++i) {
            float sample = sinf(phase) * 32767.0f * volume;
            int16_t val = (int16_t)sample;
            buffer[i * 2] = val;
            buffer[i * 2 + 1] = val;
            phase += phase_increment;
            if (phase >= 2.0f * (float)M_PI) {
                phase -= 2.0f * (float)M_PI;
            }
        }
        size_t bytes_to_write = frames_now * 2 * sizeof(int16_t);
        size_t written = 0;
        i2s_write(s_cfg.port, buffer, bytes_to_write, &written, portMAX_DELAY);
        generated += frames_now;
    }

    heap_caps_free(buffer);
    return ESP_OK;
}

#pragma pack(push, 1)
typedef struct {
    char chunk_id[4];
    uint32_t chunk_size;
    char format[4];
    char subchunk1_id[4];
    uint32_t subchunk1_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char subchunk2_id[4];
    uint32_t subchunk2_size;
} wav_header_t;
#pragma pack(pop)

esp_err_t audio_play_wav_file(const char *path) {
    if (!s_initialized || !path) {
        return ESP_ERR_INVALID_STATE;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s", path);
        return ESP_FAIL;
    }

    wav_header_t header;
    size_t read = fread(&header, 1, sizeof(header), f);
    if (read != sizeof(header) || strncmp(header.chunk_id, "RIFF", 4) != 0 || strncmp(header.format, "WAVE", 4) != 0 || strncmp(header.subchunk1_id, "fmt ", 4) != 0) {
        ESP_LOGE(TAG, "Invalid WAV header");
        fclose(f);
        return ESP_ERR_INVALID_ARG;
    }

    if (header.audio_format != 1 || header.num_channels != 2 || header.bits_per_sample != 16 || header.sample_rate != s_cfg.sample_rate_hz) {
        ESP_LOGE(TAG, "Unsupported WAV format");
        fclose(f);
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (strncmp(header.subchunk2_id, "data", 4) != 0) {
        ESP_LOGW(TAG, "Non-standard WAV, attempting to locate data chunk");
        char chunk_id[4];
        uint32_t chunk_size = 0;
        bool data_found = false;
        while (fread(chunk_id, 1, 4, f) == 4) {
            if (fread(&chunk_size, 1, 4, f) != 4) {
                break;
            }
            if (memcmp(chunk_id, "data", 4) == 0) {
                data_found = true;
                header.subchunk2_size = chunk_size;
                break;
            }
            fseek(f, chunk_size, SEEK_CUR);
        }
        if (!data_found) {
            fclose(f);
            return ESP_ERR_NOT_FOUND;
        }
    }

    uint8_t *buffer = heap_caps_malloc(AUDIO_WAV_CHUNK_BYTES, MALLOC_CAP_DMA);
    if (!buffer) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    s_stop_requested = false;
    s_is_playing = true;

    while (!s_stop_requested) {
        size_t bytes_read = fread(buffer, 1, AUDIO_WAV_CHUNK_BYTES, f);
        if (bytes_read == 0) {
            break;
        }
        size_t offset = 0;
        while (offset < bytes_read && !s_stop_requested) {
            size_t bytes_to_write = bytes_read - offset;
            size_t written = 0;
            i2s_write(s_cfg.port, buffer + offset, bytes_to_write, &written, portMAX_DELAY);
            offset += written;
        }
        if (bytes_read < AUDIO_WAV_CHUNK_BYTES) {
            break;
        }
    }

    heap_caps_free(buffer);
    fclose(f);
    s_is_playing = false;
    if (s_stop_requested) {
        s_stop_requested = false;
    }
    return ESP_OK;
}

void audio_request_stop(void) {
    s_stop_requested = true;
}

bool audio_is_playing(void) {
    return s_is_playing;
}

#pragma once

#include "esp_err.h"
#include "driver/i2c.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// PCM5242 I2C Configuration
#define PCM5242_I2C_ADDR 0x48
#define PCM5242_I2C_TIMEOUT_MS 100

// PCM5242 Register Addresses (Page 0)
#define PCM5242_REG_PAGE_SELECT 0x00
#define PCM5242_REG_POWER_CONTROL 0x01
#define PCM5242_REG_DAC_CONTROL 0x02
#define PCM5242_REG_AUDIO_SERIAL_IF 0x03
#define PCM5242_REG_CLOCK_MULTIPLIER 0x04
#define PCM5242_REG_PLL_CONTROL 0x06
#define PCM5242_REG_MCLK_DAC_CLK_DIV 0x07
#define PCM5242_REG_DAC_CLK_DIV 0x08
#define PCM5242_REG_LEFT_CH_VOLUME 0x10
#define PCM5242_REG_RIGHT_CH_VOLUME 0x11
#define PCM5242_REG_MASTER_VOLUME 0x14

// Register Values
#define PCM5242_POWER_CONTROL_VAL 0x10       // Soft Power Up
#define PCM5242_DAC_CONTROL_VAL 0x2C         // Normal operation, low-latency filter
#define PCM5242_AUDIO_SERIAL_IF_VAL 0x20     // I2S mode, 16/24/32-bit
#define PCM5242_MASTER_VOLUME_DEFAULT 0xFF   // Full volume (+24dB)

typedef struct {
    i2c_port_t i2c_port;
    gpio_num_t xsmt_pin;
} pcm5242_config_t;

/**
 * @brief Initialize PCM5242 DAC
 * 
 * @param config Configuration structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t pcm5242_init(const pcm5242_config_t *config);

/**
 * @brief Write a register to PCM5242
 * 
 * @param reg Register address
 * @param value Register value
 * @return esp_err_t ESP_OK on success
 */
esp_err_t pcm5242_write_register(uint8_t reg, uint8_t value);

/**
 * @brief Read a register from PCM5242
 * 
 * @param reg Register address
 * @param value Pointer to store read value
 * @return esp_err_t ESP_OK on success
 */
esp_err_t pcm5242_read_register(uint8_t reg, uint8_t *value);

/**
 * @brief Set master volume
 * 
 * @param volume Volume value (0x00 = -127.5dB to 0xFF = +24dB)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t pcm5242_set_volume(uint8_t volume);

/**
 * @brief Enable/Disable audio output (soft mute control)
 * 
 * @param enable true to enable audio, false to mute
 * @return esp_err_t ESP_OK on success
 */
esp_err_t pcm5242_enable_audio(bool enable);

#ifdef __cplusplus
}
#endif

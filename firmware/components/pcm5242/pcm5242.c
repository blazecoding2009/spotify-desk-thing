#include "pcm5242.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "PCM5242";
static i2c_port_t pcm5242_i2c_port = I2C_NUM_0;
static gpio_num_t pcm5242_xsmt_pin = GPIO_NUM_10;

esp_err_t pcm5242_write_register(uint8_t reg, uint8_t value) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (PCM5242_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, value, true);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(pcm5242_i2c_port, cmd, pdMS_TO_TICKS(PCM5242_I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write register 0x%02X: %s", reg, esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t pcm5242_read_register(uint8_t reg, uint8_t *value) {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (PCM5242_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (PCM5242_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, value, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(pcm5242_i2c_port, cmd, pdMS_TO_TICKS(PCM5242_I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read register 0x%02X: %s", reg, esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t pcm5242_enable_audio(bool enable) {
    gpio_set_level(pcm5242_xsmt_pin, enable ? 1 : 0);
    ESP_LOGI(TAG, "Audio output %s", enable ? "enabled" : "muted");
    return ESP_OK;
}

esp_err_t pcm5242_set_volume(uint8_t volume) {
    return pcm5242_write_register(PCM5242_REG_MASTER_VOLUME, volume);
}

esp_err_t pcm5242_init(const pcm5242_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    pcm5242_i2c_port = config->i2c_port;
    pcm5242_xsmt_pin = config->xsmt_pin;
    
    ESP_LOGI(TAG, "PCM5242 init: I2C port=%d, XSMT pin=%d", pcm5242_i2c_port, pcm5242_xsmt_pin);
    
    // Configure XSMT pin
    gpio_config_t xsmt_conf = {
        .pin_bit_mask = (1ULL << pcm5242_xsmt_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t gpio_ret = gpio_config(&xsmt_conf);
    if (gpio_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure XSMT GPIO: %s", esp_err_to_name(gpio_ret));
        return gpio_ret;
    }
    
    // Initial mute (pull XSMT low)
    gpio_set_level(pcm5242_xsmt_pin, 0);
    
    // Wait for power supplies to stabilize
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Test I2C communication - probe device first
    ESP_LOGI(TAG, "Probing PCM5242 at I2C address 0x%02X", PCM5242_I2C_ADDR);
    i2c_cmd_handle_t probe_cmd = i2c_cmd_link_create();
    i2c_master_start(probe_cmd);
    i2c_master_write_byte(probe_cmd, (PCM5242_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(probe_cmd);
    esp_err_t probe_ret = i2c_master_cmd_begin(pcm5242_i2c_port, probe_cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(probe_cmd);
    
    if (probe_ret != ESP_OK) {
        ESP_LOGW(TAG, "PCM5242 probe failed - device not responding (I2C error: %s)", esp_err_to_name(probe_ret));
        return probe_ret;
    }
    
    ESP_LOGI(TAG, "PCM5242 device found! Proceeding with initialization...");
    
    // Write initialization registers
    ESP_LOGI(TAG, "Writing PCM5242 initialization registers");
    
    // Page Select (stay on Page 0)
    esp_err_t page_ret = pcm5242_write_register(PCM5242_REG_PAGE_SELECT, 0x00);
    if (page_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to select page: %s", esp_err_to_name(page_ret));
        return page_ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Power Control - Soft Power Up
    esp_err_t power_ret = pcm5242_write_register(PCM5242_REG_POWER_CONTROL, PCM5242_POWER_CONTROL_VAL);
    if (power_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to power up DAC: %s", esp_err_to_name(power_ret));
        return power_ret;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // DAC Control - Normal operation, low-latency filter
    esp_err_t dac_ret = pcm5242_write_register(PCM5242_REG_DAC_CONTROL, PCM5242_DAC_CONTROL_VAL);
    if (dac_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to configure DAC control: %s", esp_err_to_name(dac_ret));
        return dac_ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Audio Serial Data Interface - I2S mode
    esp_err_t serial_ret = pcm5242_write_register(PCM5242_REG_AUDIO_SERIAL_IF, PCM5242_AUDIO_SERIAL_IF_VAL);
    if (serial_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to configure audio serial interface: %s", esp_err_to_name(serial_ret));
        return serial_ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Master Volume - Full volume
    esp_err_t vol_ret = pcm5242_write_register(PCM5242_REG_MASTER_VOLUME, PCM5242_MASTER_VOLUME_DEFAULT);
    if (vol_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set master volume: %s", esp_err_to_name(vol_ret));
        return vol_ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Enable audio output (pull XSMT HIGH)
    esp_err_t enable_ret = pcm5242_enable_audio(true);
    if (enable_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable audio: %s", esp_err_to_name(enable_ret));
        return enable_ret;
    }
    
    // Wait for stabilization
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ESP_LOGI(TAG, "PCM5242 initialization complete");
    
    return ESP_OK;
}

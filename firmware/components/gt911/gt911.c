#include "gt911.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "GT911"
#define GT911_I2C_ADDR 0x5D
#define GT911_REG_PRODUCT_ID 0x8140
#define GT911_REG_FW_VERSION 0x8144
#define GT911_REG_STATUS 0x814E
#define GT911_REG_POINTS 0x8150

struct gt911_handle_s {
    gt911_config_t cfg;
    gt911_int_callback_t callback;
    void *callback_data;
    volatile bool int_flag;
};

static esp_err_t gt911_write(gt911_handle_t *handle, uint16_t reg, const uint8_t *data, size_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (GT911_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg & 0xFF, true);
    i2c_master_write_byte(cmd, (reg >> 8) & 0xFF, true);
    if (data && len) {
        i2c_master_write(cmd, data, len, true);
    }
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(handle->cfg.i2c_port, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t gt911_read(gt911_handle_t *handle, uint16_t reg, uint8_t *data, size_t len) {
    uint8_t buffer[2];
    buffer[0] = reg & 0xFF;
    buffer[1] = (reg >> 8) & 0xFF;
    return i2c_master_write_read_device(handle->cfg.i2c_port, GT911_I2C_ADDR, buffer, sizeof(buffer), data, len, pdMS_TO_TICKS(50));
}

static void IRAM_ATTR gt911_gpio_isr(void *arg) {
    gt911_handle_t *handle = (gt911_handle_t *)arg;
    handle->int_flag = true;
    if (handle->callback) {
        handle->callback(handle->callback_data);
    }
}

static esp_err_t gt911_configure_gpio(const gt911_config_t *cfg) {
    gpio_config_t int_conf = {
        .pin_bit_mask = 1ULL << cfg->int_pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    return gpio_config(&int_conf);
}

esp_err_t gt911_init(gt911_handle_t **out_handle, const gt911_config_t *config) {
    if (!out_handle || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    gt911_handle_t *handle = calloc(1, sizeof(gt911_handle_t));
    if (!handle) {
        return ESP_ERR_NO_MEM;
    }

    handle->cfg = *config;

    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = config->sda_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = config->scl_pin,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = config->i2c_clock_hz,
    };

    ESP_RETURN_ON_ERROR(i2c_param_config(config->i2c_port, &i2c_conf), TAG, "I2C param config failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(config->i2c_port, i2c_conf.mode, 0, 0, 0), TAG, "I2C driver install failed");
    ESP_RETURN_ON_ERROR(gt911_configure_gpio(config), TAG, "INT pin config failed");

    uint8_t product_id[4] = {0};
    ESP_RETURN_ON_ERROR(gt911_read(handle, GT911_REG_PRODUCT_ID, product_id, sizeof(product_id)), TAG, "Read product ID failed");
    uint16_t fw_version = 0;
    ESP_RETURN_ON_ERROR(gt911_read(handle, GT911_REG_FW_VERSION, (uint8_t *)&fw_version, sizeof(fw_version)), TAG, "Read FW version failed");
    ESP_LOGI(TAG, "GT911 Product ID: %c%c%c%c FW: 0x%04X", product_id[0], product_id[1], product_id[2], product_id[3], fw_version);

    *out_handle = handle;
    return ESP_OK;
}

esp_err_t gt911_set_interrupt_callback(gt911_handle_t *handle, gt911_int_callback_t cb, void *user_data) {
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    handle->callback = cb;
    handle->callback_data = user_data;

    if (cb) {
        esp_err_t err = gpio_install_isr_service(0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
        ESP_RETURN_ON_ERROR(gpio_isr_handler_add(handle->cfg.int_pin, gt911_gpio_isr, handle), TAG, "ISR add failed");
    } else {
        gpio_isr_handler_remove(handle->cfg.int_pin);
    }
    return ESP_OK;
}

void gt911_clear_interrupt(gt911_handle_t *handle) {
    if (!handle) {
        return;
    }
    uint8_t clear = 0;
    gt911_write(handle, GT911_REG_STATUS, &clear, 1);
    handle->int_flag = false;
}

esp_err_t gt911_read_touch_points(gt911_handle_t *handle, gt911_touch_data_t *touches) {
    if (!handle || !touches) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t status = 0;
    esp_err_t err = gt911_read(handle, GT911_REG_STATUS, &status, 1);
    if (err != ESP_OK) {
        return err;
    }

    if (!(status & 0x80)) {
        touches->num_points = 0;
        return ESP_OK;
    }

    uint8_t points = status & 0x0F;
    if (points > GT911_MAX_TOUCHES) {
        points = GT911_MAX_TOUCHES;
    }

    size_t bytes_to_read = points * 8;
    uint8_t buffer[GT911_MAX_TOUCHES * 8] = {0};
    if (bytes_to_read) {
        ESP_RETURN_ON_ERROR(gt911_read(handle, GT911_REG_POINTS, buffer, bytes_to_read), TAG, "Read points failed");
    }

    touches->num_points = points;
    for (uint8_t i = 0; i < points; ++i) {
        const uint8_t *entry = &buffer[i * 8];
        touches->points[i].id = entry[0];
        touches->points[i].x = entry[1] | (entry[2] << 8);
        touches->points[i].y = entry[3] | (entry[4] << 8);
        touches->points[i].size = entry[5] | (entry[6] << 8);
    }

    gt911_clear_interrupt(handle);
    return ESP_OK;
}

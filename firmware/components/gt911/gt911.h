#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"

#define GT911_MAX_TOUCHES 5

typedef struct {
    uint8_t id;
    uint16_t x;
    uint16_t y;
    uint16_t size;
} gt911_point_t;

typedef struct {
    uint8_t num_points;
    gt911_point_t points[GT911_MAX_TOUCHES];
} gt911_touch_data_t;

typedef void (*gt911_int_callback_t)(void *user_data);

typedef struct {
    i2c_port_t i2c_port;
    gpio_num_t sda_pin;
    gpio_num_t scl_pin;
    gpio_num_t int_pin;
    uint32_t i2c_clock_hz;
} gt911_config_t;

typedef struct gt911_handle_s gt911_handle_t;

esp_err_t gt911_init(gt911_handle_t **handle, const gt911_config_t *config);
esp_err_t gt911_read_touch_points(gt911_handle_t *handle, gt911_touch_data_t *touches);
esp_err_t gt911_set_interrupt_callback(gt911_handle_t *handle, gt911_int_callback_t cb, void *user_data);
void gt911_clear_interrupt(gt911_handle_t *handle);

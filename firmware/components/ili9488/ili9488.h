#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#define ILI9488_WIDTH 320
#define ILI9488_HEIGHT 480

typedef struct {
    spi_device_handle_t spi;
    gpio_num_t dc_pin;
    gpio_num_t reset_pin;
    gpio_num_t backlight_pin;
    bool backlight_active_high;
} ili9488_t;

typedef struct {
    spi_device_handle_t spi;
    gpio_num_t dc_pin;
    gpio_num_t reset_pin;
    gpio_num_t backlight_pin;
    bool backlight_active_high;
} ili9488_config_t;

esp_err_t ili9488_init(ili9488_t *lcd, const ili9488_config_t *config);
esp_err_t ili9488_set_window(ili9488_t *lcd, uint16_t x, uint16_t y, uint16_t width, uint16_t height);
esp_err_t ili9488_fill_color(ili9488_t *lcd, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);
esp_err_t ili9488_draw_rgb565_bitmap(ili9488_t *lcd, uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint16_t *bitmap);
esp_err_t ili9488_set_backlight(ili9488_t *lcd, bool enable);

void ili9488_delay_ms(uint32_t ms);

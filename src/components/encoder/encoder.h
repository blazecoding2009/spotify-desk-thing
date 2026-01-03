#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
    ENCODER_EVENT_NONE = 0,
    ENCODER_EVENT_LEFT,
    ENCODER_EVENT_RIGHT,
    ENCODER_EVENT_BUTTON,
} encoder_event_type_t;

typedef struct {
    encoder_event_type_t type;
    TickType_t timestamp;
} encoder_event_t;

typedef struct {
    gpio_num_t pin_a;
    gpio_num_t pin_b;
    gpio_num_t pin_button;
    bool button_active_level_low;
    uint32_t debounce_ms;
} encoder_config_t;

typedef struct encoder_handle_s encoder_handle_t;

esp_err_t encoder_init(encoder_handle_t **handle, const encoder_config_t *config);
bool encoder_get_event(encoder_handle_t *handle, encoder_event_t *event, TickType_t ticks_to_wait);
void encoder_reset(encoder_handle_t *handle);

#include "encoder.h"

#include <stdlib.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG "ENCODER"
#define ENCODER_QUEUE_LENGTH 16

struct encoder_handle_s {
    encoder_config_t cfg;
    QueueHandle_t queue;
    volatile uint8_t last_state;
    volatile int64_t last_step_us;
    volatile int64_t last_button_us;
};

static const int8_t transition_table[16] = {
    0, -1, 1, 0,
    1, 0, 0, -1,
    -1, 0, 0, 1,
    0, 1, -1, 0
};

static void send_event_from_isr(encoder_handle_t *handle, encoder_event_type_t type) {
    encoder_event_t event = {
        .type = type,
        .timestamp = xTaskGetTickCountFromISR(),
    };
    BaseType_t hp_task = pdFALSE;
    xQueueSendFromISR(handle->queue, &event, &hp_task);
    if (hp_task == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void IRAM_ATTR encoder_ab_isr(void *arg) {
    encoder_handle_t *handle = (encoder_handle_t *)arg;
    uint8_t a = gpio_get_level(handle->cfg.pin_a);
    uint8_t b = gpio_get_level(handle->cfg.pin_b);
    uint8_t current_state = (a << 1) | b;
    uint8_t index = ((handle->last_state << 2) | current_state) & 0x0F;
    int8_t movement = transition_table[index];
    handle->last_state = current_state;
    if (movement != 0) {
        int64_t now = esp_timer_get_time();
        if (now - handle->last_step_us >= (int64_t)handle->cfg.debounce_ms * 1000) {
            handle->last_step_us = now;
            send_event_from_isr(handle, movement > 0 ? ENCODER_EVENT_RIGHT : ENCODER_EVENT_LEFT);
        }
    }
}

static void IRAM_ATTR encoder_button_isr(void *arg) {
    encoder_handle_t *handle = (encoder_handle_t *)arg;
    int level = gpio_get_level(handle->cfg.pin_button);
    bool active = handle->cfg.button_active_level_low ? level == 0 : level == 1;
    if (!active) {
        return;
    }
    int64_t now = esp_timer_get_time();
    if (now - handle->last_button_us >= (int64_t)handle->cfg.debounce_ms * 1000) {
        handle->last_button_us = now;
        send_event_from_isr(handle, ENCODER_EVENT_BUTTON);
    }
}

esp_err_t encoder_init(encoder_handle_t **out_handle, const encoder_config_t *config) {
    if (!out_handle || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    encoder_handle_t *handle = calloc(1, sizeof(encoder_handle_t));
    if (!handle) {
        return ESP_ERR_NO_MEM;
    }
    handle->cfg = *config;
    handle->queue = xQueueCreate(ENCODER_QUEUE_LENGTH, sizeof(encoder_event_t));
    if (!handle->queue) {
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    gpio_config_t gpio_conf = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_conf.pin_bit_mask = (1ULL << config->pin_a) | (1ULL << config->pin_b);
    ESP_RETURN_ON_ERROR(gpio_config(&gpio_conf), TAG, "Config AB failed");

    gpio_conf.pin_bit_mask = (1ULL << config->pin_button);
    gpio_conf.intr_type = GPIO_INTR_ANYEDGE;
    gpio_conf.pull_up_en = config->button_active_level_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    gpio_conf.pull_down_en = config->button_active_level_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&gpio_conf), TAG, "Config button failed");

    handle->last_state = ((gpio_get_level(config->pin_a) << 1) | gpio_get_level(config->pin_b)) & 0x03;
    handle->last_step_us = esp_timer_get_time();
    handle->last_button_us = 0;

    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        vQueueDelete(handle->queue);
        free(handle);
        return err;
    }

    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(config->pin_a, encoder_ab_isr, handle), TAG, "ISR A failed");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(config->pin_b, encoder_ab_isr, handle), TAG, "ISR B failed");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(config->pin_button, encoder_button_isr, handle), TAG, "ISR button failed");

    *out_handle = handle;
    return ESP_OK;
}

bool encoder_get_event(encoder_handle_t *handle, encoder_event_t *event, TickType_t ticks_to_wait) {
    if (!handle || !event) {
        return false;
    }
    if (xQueueReceive(handle->queue, event, ticks_to_wait) == pdPASS) {
        return true;
    }
    return false;
}

void encoder_reset(encoder_handle_t *handle) {
    if (!handle) {
        return;
    }
    xQueueReset(handle->queue);
    handle->last_state = ((gpio_get_level(handle->cfg.pin_a) << 1) | gpio_get_level(handle->cfg.pin_b)) & 0x03;
}

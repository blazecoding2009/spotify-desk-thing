#include "ili9488.h"

#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "ILI9488"
#define ILI9488_CMD_CASET 0x2A
#define ILI9488_CMD_PASET 0x2B
#define ILI9488_CMD_RAMWR 0x2C
#define ILI9488_CMD_MADCTL 0x36
#define ILI9488_CMD_PIXFMT 0x3A
#define ILI9488_CMD_SLPOUT 0x11
#define ILI9488_CMD_DISPON 0x29

#define ILI9488_CHUNK_PIXELS 1024

typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t data_len;
    uint16_t delay_ms;
} ili9488_init_cmd_t;

static const ili9488_init_cmd_t init_cmds[] = {
    {0xE0, {0x00, 0x03, 0x09, 0x08, 0x16, 0x0A, 0x3F, 0x78, 0x4C, 0x09, 0x0A, 0x08, 0x16, 0x1A, 0x0F}, 15, 0},
    {0xE1, {0x00, 0x16, 0x19, 0x03, 0x0F, 0x05, 0x32, 0x45, 0x46, 0x04, 0x0E, 0x0D, 0x35, 0x37, 0x0F}, 15, 0},
    {0xC0, {0x17, 0x15}, 2, 0},
    {0xC1, {0x41}, 1, 0},
    {0xC5, {0x00, 0x12, 0x80}, 3, 0},
    {ILI9488_CMD_MADCTL, {0x48}, 1, 0},
    {ILI9488_CMD_PIXFMT, {0x55}, 1, 0},
    {0xB0, {0x00}, 1, 0},
    {0xB1, {0xA0}, 1, 0},
    {0xB4, {0x02}, 1, 0},
    {0xB6, {0x02, 0x02}, 2, 0},
    {0xB7, {0xC6}, 1, 0},
    {0xE9, {0x00}, 1, 0},
    {0xF7, {0xA9, 0x51, 0x2C, 0x82}, 4, 0},
    {ILI9488_CMD_SLPOUT, {0}, 0, 120},
    {ILI9488_CMD_DISPON, {0}, 0, 20},
};

static esp_err_t ili9488_send_cmd(ili9488_t *lcd, uint8_t cmd, const uint8_t *data, size_t len) {
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_TXDATA,
        .length = 8,
        .tx_data = {cmd, 0, 0, 0},
    };
    gpio_set_level(lcd->dc_pin, 0);
    esp_err_t ret = spi_device_polling_transmit(lcd->spi, &t);
    if (ret != ESP_OK) {
        return ret;
    }
    if (data && len) {
        spi_transaction_t data_trans = {
            .length = len * 8,
            .tx_buffer = data,
        };
        gpio_set_level(lcd->dc_pin, 1);
        ret = spi_device_polling_transmit(lcd->spi, &data_trans);
    }
    return ret;
}

static esp_err_t ili9488_write_reg16(ili9488_t *lcd, uint8_t cmd, uint16_t start, uint16_t end) {
    uint8_t data[4] = {
        (uint8_t)(start >> 8),
        (uint8_t)(start & 0xFF),
        (uint8_t)(end >> 8),
        (uint8_t)(end & 0xFF),
    };
    return ili9488_send_cmd(lcd, cmd, data, sizeof(data));
}

void ili9488_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void ili9488_hw_reset(ili9488_t *lcd) {
    if (lcd->reset_pin == GPIO_NUM_NC) {
        return;
    }
    gpio_set_level(lcd->reset_pin, 0);
    ili9488_delay_ms(20);
    gpio_set_level(lcd->reset_pin, 1);
    ili9488_delay_ms(120);
}

static void ili9488_config_pins(ili9488_t *lcd) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << lcd->dc_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    if (lcd->reset_pin != GPIO_NUM_NC) {
        io_conf.pin_bit_mask = (1ULL << lcd->reset_pin);
        gpio_config(&io_conf);
    }

    if (lcd->backlight_pin != GPIO_NUM_NC) {
        io_conf.pin_bit_mask = (1ULL << lcd->backlight_pin);
        gpio_config(&io_conf);
    }
}

esp_err_t ili9488_init(ili9488_t *lcd, const ili9488_config_t *config) {
    if (!lcd || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    *lcd = (ili9488_t) {
        .spi = config->spi,
        .dc_pin = config->dc_pin,
        .reset_pin = config->reset_pin,
        .backlight_pin = config->backlight_pin,
        .backlight_active_high = config->backlight_active_high,
    };

    ili9488_config_pins(lcd);
    ili9488_hw_reset(lcd);

    for (size_t i = 0; i < sizeof(init_cmds) / sizeof(init_cmds[0]); ++i) {
        const ili9488_init_cmd_t *entry = &init_cmds[i];
        ESP_RETURN_ON_ERROR(ili9488_send_cmd(lcd, entry->cmd, entry->data_len ? entry->data : NULL, entry->data_len), TAG, "Init cmd failed");
        if (entry->delay_ms) {
            ili9488_delay_ms(entry->delay_ms);
        }
    }

    if (lcd->backlight_pin != GPIO_NUM_NC) {
        ili9488_set_backlight(lcd, true);
    }

    return ESP_OK;
}

esp_err_t ili9488_set_window(ili9488_t *lcd, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    uint16_t x_end = x + width - 1;
    uint16_t y_end = y + height - 1;
    ESP_RETURN_ON_ERROR(ili9488_write_reg16(lcd, ILI9488_CMD_CASET, x, x_end), TAG, "CASET failed");
    ESP_RETURN_ON_ERROR(ili9488_write_reg16(lcd, ILI9488_CMD_PASET, y, y_end), TAG, "PASET failed");
    return ili9488_send_cmd(lcd, ILI9488_CMD_RAMWR, NULL, 0);
}

esp_err_t ili9488_draw_rgb565_bitmap(ili9488_t *lcd, uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint16_t *bitmap) {
    if (!lcd || !bitmap) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t total_pixels = width * height;
    ESP_RETURN_ON_ERROR(ili9488_set_window(lcd, x, y, width, height), TAG, "Set window failed");

    uint8_t *chunk = heap_caps_malloc(ILI9488_CHUNK_PIXELS * 2, MALLOC_CAP_DMA);
    if (!chunk) {
        ESP_LOGE(TAG, "Failed to allocate DMA chunk");
        return ESP_ERR_NO_MEM;
    }

    size_t offset = 0;
    while (offset < total_pixels) {
        size_t chunk_pixels = total_pixels - offset;
        if (chunk_pixels > ILI9488_CHUNK_PIXELS) {
            chunk_pixels = ILI9488_CHUNK_PIXELS;
        }
        for (size_t i = 0; i < chunk_pixels; ++i) {
            uint16_t color = bitmap[offset + i];
            chunk[i * 2] = color >> 8;
            chunk[i * 2 + 1] = color & 0xFF;
        }
        spi_transaction_t trans = {
            .length = chunk_pixels * 16,
            .tx_buffer = chunk,
        };
        gpio_set_level(lcd->dc_pin, 1);
        ESP_RETURN_ON_ERROR(spi_device_polling_transmit(lcd->spi, &trans), TAG, "RAMWR chunk failed");
        offset += chunk_pixels;
    }

    heap_caps_free(chunk);
    return ESP_OK;
}

esp_err_t ili9488_fill_color(ili9488_t *lcd, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color) {
    size_t total_pixels = width * height;
    ESP_RETURN_ON_ERROR(ili9488_set_window(lcd, x, y, width, height), TAG, "Set window failed");

    uint8_t *chunk = heap_caps_malloc(ILI9488_CHUNK_PIXELS * 2, MALLOC_CAP_DMA);
    if (!chunk) {
        return ESP_ERR_NO_MEM;
    }

    const uint8_t hi = color >> 8;
    const uint8_t lo = color & 0xFF;
    size_t max_pixels = ILI9488_CHUNK_PIXELS;

    while (total_pixels > 0) {
        size_t chunk_pixels = total_pixels > max_pixels ? max_pixels : total_pixels;
        for (size_t i = 0; i < chunk_pixels; ++i) {
            chunk[i * 2] = hi;
            chunk[i * 2 + 1] = lo;
        }
        spi_transaction_t trans = {
            .length = chunk_pixels * 16,
            .tx_buffer = chunk,
        };
        gpio_set_level(lcd->dc_pin, 1);
        ESP_RETURN_ON_ERROR(spi_device_polling_transmit(lcd->spi, &trans), TAG, "Fill chunk failed");
        total_pixels -= chunk_pixels;
    }

    heap_caps_free(chunk);
    return ESP_OK;
}

esp_err_t ili9488_set_backlight(ili9488_t *lcd, bool enable) {
    if (!lcd || lcd->backlight_pin == GPIO_NUM_NC) {
        return ESP_ERR_INVALID_ARG;
    }
    int level = (enable == lcd->backlight_active_high) ? 1 : 0;
    gpio_set_level(lcd->backlight_pin, level);
    return ESP_OK;
}

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "ili9488.h"

typedef struct {
    ili9488_t *display;
    uint16_t background_color;
    uint16_t accent_color;
} ui_config_t;

typedef struct {
    ili9488_t *display;
    uint16_t background_color;
    uint16_t accent_color;
    uint8_t volume_percent;
    bool is_playing;
    char track_name[64];
} ui_context_t;

esp_err_t ui_init(ui_context_t *ctx, const ui_config_t *config);
void ui_draw_boot_screen(ui_context_t *ctx);
void ui_set_track(ui_context_t *ctx, const char *track);
void ui_set_volume(ui_context_t *ctx, uint8_t volume_percent);
void ui_set_play_state(ui_context_t *ctx, bool playing);
void ui_redraw(ui_context_t *ctx);

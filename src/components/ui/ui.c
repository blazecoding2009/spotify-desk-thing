#include "ui.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include "esp_log.h"

#define TAG "UI"
#define UI_FONT_WIDTH 5
#define UI_FONT_HEIGHT 7
#define UI_FONT_MAX_SCALE 3
#define UI_PADDING 16
#define UI_VOLUME_BAR_WIDTH 220
#define UI_VOLUME_BAR_HEIGHT 20
#define UI_PLAY_ICON_SIZE 48

typedef struct {
    char ch;
    uint8_t rows[UI_FONT_HEIGHT];
} ui_glyph_t;

#define ROW(a, b, c, d, e) ((a << 4) | (b << 3) | (c << 2) | (d << 1) | (e))

static const ui_glyph_t font_map[] = {
    {' ', {0, 0, 0, 0, 0, 0, 0}},
    {'-', {0, 0, 0, ROW(0,1,1,1,0), 0, 0, 0}},
    {'.', {0,0,0,0,0, ROW(0,1,1,0,0), ROW(0,1,1,0,0)}},
    {'0', {ROW(0,1,1,1,0), ROW(1,0,0,0,1), ROW(1,0,0,1,1), ROW(1,0,1,0,1), ROW(1,1,0,0,1), ROW(1,0,0,0,1), ROW(0,1,1,1,0)}},
    {'1', {ROW(0,0,1,0,0), ROW(0,1,1,0,0), ROW(1,0,1,0,0), ROW(0,0,1,0,0), ROW(0,0,1,0,0), ROW(0,0,1,0,0), ROW(1,1,1,1,1)}},
    {'2', {ROW(0,1,1,1,0), ROW(1,0,0,0,1), ROW(0,0,0,0,1), ROW(0,0,0,1,0), ROW(0,0,1,0,0), ROW(0,1,0,0,0), ROW(1,1,1,1,1)}},
    {'3', {ROW(1,1,1,1,0), ROW(0,0,0,0,1), ROW(0,0,0,1,0), ROW(0,0,1,1,0), ROW(0,0,0,0,1), ROW(1,0,0,0,1), ROW(0,1,1,1,0)}},
    {'4', {ROW(0,0,0,1,0), ROW(0,0,1,1,0), ROW(0,1,0,1,0), ROW(1,0,0,1,0), ROW(1,1,1,1,1), ROW(0,0,0,1,0), ROW(0,0,0,1,0)}},
    {'5', {ROW(1,1,1,1,1), ROW(1,0,0,0,0), ROW(1,1,1,1,0), ROW(0,0,0,0,1), ROW(0,0,0,0,1), ROW(1,0,0,0,1), ROW(0,1,1,1,0)}},
    {'6', {ROW(0,0,1,1,0), ROW(0,1,0,0,0), ROW(1,0,0,0,0), ROW(1,1,1,1,0), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(0,1,1,1,0)}},
    {'7', {ROW(1,1,1,1,1), ROW(0,0,0,0,1), ROW(0,0,0,1,0), ROW(0,0,1,0,0), ROW(0,1,0,0,0), ROW(0,1,0,0,0), ROW(0,1,0,0,0)}},
    {'8', {ROW(0,1,1,1,0), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(0,1,1,1,0), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(0,1,1,1,0)}},
    {'9', {ROW(0,1,1,1,0), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(0,1,1,1,1), ROW(0,0,0,0,1), ROW(0,0,0,1,0), ROW(0,1,1,0,0)}},
    {'A', {ROW(0,1,1,1,0), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,1,1,1,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1)}},
    {'B', {ROW(1,1,1,1,0), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,1,1,1,0), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,1,1,1,0)}},
    {'C', {ROW(0,1,1,1,0), ROW(1,0,0,0,1), ROW(1,0,0,0,0), ROW(1,0,0,0,0), ROW(1,0,0,0,0), ROW(1,0,0,0,1), ROW(0,1,1,1,0)}},
    {'D', {ROW(1,1,1,0,0), ROW(1,0,0,1,0), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,0,0,1,0), ROW(1,1,1,0,0)}},
    {'E', {ROW(1,1,1,1,1), ROW(1,0,0,0,0), ROW(1,0,0,0,0), ROW(1,1,1,1,0), ROW(1,0,0,0,0), ROW(1,0,0,0,0), ROW(1,1,1,1,1)}},
    {'F', {ROW(1,1,1,1,1), ROW(1,0,0,0,0), ROW(1,0,0,0,0), ROW(1,1,1,1,0), ROW(1,0,0,0,0), ROW(1,0,0,0,0), ROW(1,0,0,0,0)}},
    {'G', {ROW(0,1,1,1,0), ROW(1,0,0,0,1), ROW(1,0,0,0,0), ROW(1,0,1,1,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(0,1,1,1,0)}},
    {'H', {ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,1,1,1,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1)}},
    {'I', {ROW(0,1,1,1,0), ROW(0,0,1,0,0), ROW(0,0,1,0,0), ROW(0,0,1,0,0), ROW(0,0,1,0,0), ROW(0,0,1,0,0), ROW(0,1,1,1,0)}},
    {'J', {ROW(0,0,0,1,1), ROW(0,0,0,0,1), ROW(0,0,0,0,1), ROW(0,0,0,0,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(0,1,1,1,0)}},
    {'K', {ROW(1,0,0,0,1), ROW(1,0,0,1,0), ROW(1,0,1,0,0), ROW(1,1,0,0,0), ROW(1,0,1,0,0), ROW(1,0,0,1,0), ROW(1,0,0,0,1)}},
    {'L', {ROW(1,0,0,0,0), ROW(1,0,0,0,0), ROW(1,0,0,0,0), ROW(1,0,0,0,0), ROW(1,0,0,0,0), ROW(1,0,0,0,0), ROW(1,1,1,1,1)}},
    {'M', {ROW(1,0,0,0,1), ROW(1,1,0,1,1), ROW(1,0,1,0,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1)}},
    {'N', {ROW(1,0,0,0,1), ROW(1,1,0,0,1), ROW(1,0,1,0,1), ROW(1,0,0,1,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1)}},
    {'O', {ROW(0,1,1,1,0), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(0,1,1,1,0)}},
    {'P', {ROW(1,1,1,1,0), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,1,1,1,0), ROW(1,0,0,0,0), ROW(1,0,0,0,0), ROW(1,0,0,0,0)}},
    {'Q', {ROW(0,1,1,1,0), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,0,0,1,1), ROW(1,0,0,0,1), ROW(0,1,1,1,1)}},
    {'R', {ROW(1,1,1,1,0), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,1,1,1,0), ROW(1,0,1,0,0), ROW(1,0,0,1,0), ROW(1,0,0,0,1)}},
    {'S', {ROW(0,1,1,1,0), ROW(1,0,0,0,1), ROW(1,0,0,0,0), ROW(0,1,1,1,0), ROW(0,0,0,0,1), ROW(1,0,0,0,1), ROW(0,1,1,1,0)}},
    {'T', {ROW(1,1,1,1,1), ROW(0,0,1,0,0), ROW(0,0,1,0,0), ROW(0,0,1,0,0), ROW(0,0,1,0,0), ROW(0,0,1,0,0), ROW(0,0,1,0,0)}},
    {'U', {ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(0,1,1,1,0)}},
    {'V', {ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(0,1,0,1,0), ROW(0,1,0,1,0), ROW(0,0,1,0,0)}},
    {'W', {ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,0,0,0,1), ROW(1,0,1,0,1), ROW(1,0,1,0,1), ROW(1,0,1,0,1), ROW(0,1,0,1,0)}},
    {'X', {ROW(1,0,0,0,1), ROW(0,1,0,1,0), ROW(0,1,0,1,0), ROW(0,0,1,0,0), ROW(0,1,0,1,0), ROW(0,1,0,1,0), ROW(1,0,0,0,1)}},
    {'Y', {ROW(1,0,0,0,1), ROW(0,1,0,1,0), ROW(0,1,0,1,0), ROW(0,0,1,0,0), ROW(0,0,1,0,0), ROW(0,0,1,0,0), ROW(0,0,1,0,0)}},
    {'Z', {ROW(1,1,1,1,1), ROW(0,0,0,0,1), ROW(0,0,0,1,0), ROW(0,0,1,0,0), ROW(0,1,0,0,0), ROW(1,0,0,0,0), ROW(1,1,1,1,1)}},
    {'?', {ROW(0,1,1,1,0), ROW(1,0,0,0,1), ROW(0,0,0,0,1), ROW(0,0,0,1,0), ROW(0,0,0,1,0), ROW(0,0,0,0,0), ROW(0,0,0,1,0)}},
};

static uint16_t ui_color(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static const ui_glyph_t *ui_find_glyph(char input) {
    char target = (char)toupper((int)input);
    size_t count = sizeof(font_map) / sizeof(font_map[0]);
    for (size_t i = 0; i < count; ++i) {
        if (font_map[i].ch == target) {
            return &font_map[i];
        }
    }
    return &font_map[count - 1]; // '?' fallback
}

static void ui_draw_char(ui_context_t *ctx, int x, int y, char c, uint8_t scale, uint16_t fg, uint16_t bg) {
    const ui_glyph_t *glyph = ui_find_glyph(c);
    const int width = UI_FONT_WIDTH * scale;
    const int height = UI_FONT_HEIGHT * scale;
    uint16_t bitmap[UI_FONT_WIDTH * UI_FONT_HEIGHT * UI_FONT_MAX_SCALE * UI_FONT_MAX_SCALE];
    for (int row = 0; row < UI_FONT_HEIGHT; ++row) {
        for (int col = 0; col < UI_FONT_WIDTH; ++col) {
            bool pixel = glyph->rows[row] & (1 << (UI_FONT_WIDTH - 1 - col));
            uint16_t color = pixel ? fg : bg;
            for (uint8_t ys = 0; ys < scale; ++ys) {
                for (uint8_t xs = 0; xs < scale; ++xs) {
                    int dst_row = row * scale + ys;
                    int dst_col = col * scale + xs;
                    bitmap[dst_row * width + dst_col] = color;
                }
            }
        }
    }
    ili9488_draw_rgb565_bitmap(ctx->display, x, y, width, height, bitmap);
}

static void ui_draw_text(ui_context_t *ctx, int x, int y, const char *text, uint8_t scale, uint16_t fg, uint16_t bg) {
    if (!text) {
        return;
    }
    int cursor_x = x;
    for (const char *p = text; *p; ++p) {
        if (*p == '\n') {
            cursor_x = x;
            y += (UI_FONT_HEIGHT + 2) * scale;
            continue;
        }
        ui_draw_char(ctx, cursor_x, y, *p, scale, fg, bg);
        cursor_x += (UI_FONT_WIDTH + 1) * scale;
    }
}

static void ui_draw_volume_bar(ui_context_t *ctx) {
    int x = UI_PADDING;
    int y = ILI9488_HEIGHT - UI_PADDING - UI_VOLUME_BAR_HEIGHT;
    uint16_t border_color = ctx->accent_color;
    uint16_t bar_bg = ui_color(30, 30, 30);
    ili9488_fill_color(ctx->display, x - 2, y - 2, UI_VOLUME_BAR_WIDTH + 4, UI_VOLUME_BAR_HEIGHT + 4, border_color);
    ili9488_fill_color(ctx->display, x, y, UI_VOLUME_BAR_WIDTH, UI_VOLUME_BAR_HEIGHT, bar_bg);
    int filled = (ctx->volume_percent * UI_VOLUME_BAR_WIDTH) / 100;
    if (filled > 0) {
        ili9488_fill_color(ctx->display, x, y, filled, UI_VOLUME_BAR_HEIGHT, ctx->accent_color);
    }
}

static void ui_draw_play_pause_icon(ui_context_t *ctx) {
    int x = ILI9488_WIDTH - UI_PADDING - UI_PLAY_ICON_SIZE;
    int y = ILI9488_HEIGHT - UI_PADDING - UI_PLAY_ICON_SIZE - UI_VOLUME_BAR_HEIGHT - 12;
    uint16_t bg = ctx->background_color;
    uint16_t fg = ctx->accent_color;
    uint16_t icon[UI_PLAY_ICON_SIZE * UI_PLAY_ICON_SIZE];
    for (int i = 0; i < UI_PLAY_ICON_SIZE * UI_PLAY_ICON_SIZE; ++i) {
        icon[i] = bg;
    }

    if (ctx->is_playing) {
        for (int row = 8; row < UI_PLAY_ICON_SIZE - 8; ++row) {
            for (int col = 10; col < 18; ++col) {
                icon[row * UI_PLAY_ICON_SIZE + col] = fg;
            }
            for (int col = 24; col < 32; ++col) {
                icon[row * UI_PLAY_ICON_SIZE + col] = fg;
            }
        }
    } else {
        for (int row = 8; row < UI_PLAY_ICON_SIZE - 8; ++row) {
            int width = (row - 8) * (UI_PLAY_ICON_SIZE - 16) / (UI_PLAY_ICON_SIZE - 16);
            for (int col = 8; col < 8 + width; ++col) {
                icon[row * UI_PLAY_ICON_SIZE + col] = fg;
            }
        }
    }

    ili9488_draw_rgb565_bitmap(ctx->display, x, y, UI_PLAY_ICON_SIZE, UI_PLAY_ICON_SIZE, icon);
}

static void ui_draw_labels(ui_context_t *ctx) {
    ui_draw_text(ctx, UI_PADDING, UI_PADDING, "NOW PLAYING", 2, ctx->accent_color, ctx->background_color);
    ui_draw_text(ctx, UI_PADDING, UI_PADDING + 40, ctx->track_name, 2, ui_color(200, 200, 200), ctx->background_color);
}

esp_err_t ui_init(ui_context_t *ctx, const ui_config_t *config) {
    if (!ctx || !config || !config->display) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->display = config->display;
    ctx->background_color = config->background_color ? config->background_color : ui_color(10, 10, 20);
    ctx->accent_color = config->accent_color ? config->accent_color : ui_color(0, 180, 255);
    ctx->volume_percent = 50;
    ctx->is_playing = false;
    strcpy(ctx->track_name, "TRACK NAME");
    return ESP_OK;
}

void ui_draw_boot_screen(ui_context_t *ctx) {
    if (!ctx || !ctx->display) {
        return;
    }
    ili9488_fill_color(ctx->display, 0, 0, ILI9488_WIDTH, ILI9488_HEIGHT, ctx->background_color);
    ui_draw_labels(ctx);
    ui_draw_volume_bar(ctx);
    ui_draw_play_pause_icon(ctx);
}

void ui_set_track(ui_context_t *ctx, const char *track) {
    if (!ctx || !track) {
        return;
    }
    strncpy(ctx->track_name, track, sizeof(ctx->track_name) - 1);
    ctx->track_name[sizeof(ctx->track_name) - 1] = '\0';
    for (size_t i = 0; ctx->track_name[i]; ++i) {
        ctx->track_name[i] = (char)toupper((int)ctx->track_name[i]);
    }
    ui_draw_labels(ctx);
}

void ui_set_volume(ui_context_t *ctx, uint8_t volume_percent) {
    if (!ctx) {
        return;
    }
    if (volume_percent > 100) {
        volume_percent = 100;
    }
    ctx->volume_percent = volume_percent;
    ui_draw_volume_bar(ctx);
}

void ui_set_play_state(ui_context_t *ctx, bool playing) {
    if (!ctx) {
        return;
    }
    ctx->is_playing = playing;
    ui_draw_play_pause_icon(ctx);
}

void ui_redraw(ui_context_t *ctx) {
    if (!ctx) {
        return;
    }
    ui_draw_boot_screen(ctx);
}

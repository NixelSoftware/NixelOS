#include "startscreen.h"
#include "../../../kernel/drivers/display/font.h"
#include "../../../kernel/drivers/display.h"
#include "logo_data.h"
#include "../../graphics/wm/desktop.h"

static uint8_t splash_active = 0;
static uint32_t spin_frame = 0;

#define SPIN_DOTS      12
#define SPIN_RADIUS    26
#define SPIN_DOT_SIZE  6
#define SPIN_BURST     14
#define SPIN_DELAY     9000
#define COPYRIGHT_TEXT "Copyright (c) 2026 NixelSoftware"

static const int kCos256[SPIN_DOTS] = {
    256, 222, 128, 0, -128, -222, -256, -222, -128, 0, 128, 222
};
static const int kSin256[SPIN_DOTS] = {
    0, 128, 222, 256, 222, 128, 0, -128, -222, -256, -222, -128
};

static int str_len(const char* s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

static void draw_spinner(int cx, int cy, uint32_t frame) {
    int head = (int)(frame % SPIN_DOTS);
    for (int i = 0; i < SPIN_DOTS; i++) {
        int dist  = (i - head + SPIN_DOTS) % SPIN_DOTS;
        int level = 235 - dist * 18;
        if (level < 30) level = 30;
        uint32_t color = ((uint32_t)level << 16) | ((uint32_t)level << 8) | (uint32_t)level;

        int px = cx + (kCos256[i] * SPIN_RADIUS) / 256 - SPIN_DOT_SIZE / 2;
        int py = cy + (kSin256[i] * SPIN_RADIUS) / 256 - SPIN_DOT_SIZE / 2;
        display_fill_rect(px, py, SPIN_DOT_SIZE, SPIN_DOT_SIZE, color);
    }
}

static void draw_logo(int logo_x, int logo_y, int scale) {
    for (int y = 0; y < LOGO_HEIGHT; y++) {
        for (int x = 0; x < LOGO_WIDTH; x++) {
            uint32_t pixel = nixel_logo_64x64[y * LOGO_WIDTH + x];
            uint8_t r = (pixel >> 16) & 0xFF;
            uint8_t g = (pixel >> 8) & 0xFF;
            uint8_t b = pixel & 0xFF;
            int is_bg = (r > 175 && g > 195 && b > 235) || (pixel == 0x00FFFFFF);
            if (!is_bg) {
                display_fill_rect(logo_x + x * scale, logo_y + y * scale,
                                  scale, scale, pixel);
            }
        }
    }
}

void startscreen_show(const char* status_text, int percent) {
    (void)status_text;
    (void)percent;
    splash_active = 1;

    int scale  = (SCREEN_HEIGHT < 700) ? 3 : 4;
    int logo_w = LOGO_WIDTH  * scale;
    int logo_h = LOGO_HEIGHT * scale;

    int spin_box = 2 * SPIN_RADIUS + SPIN_DOT_SIZE;
    int gap_logo_spin = 44;
    int gap_spin_text = 30;
    int text_h = 8;

    int group_h   = logo_h + gap_logo_spin + spin_box + gap_spin_text + text_h;
    int group_top = (SCREEN_HEIGHT - group_h) / 2;
    if (group_top < 0) group_top = 0;

    int logo_x = (SCREEN_WIDTH - logo_w) / 2;
    int logo_y = group_top;

    int spin_cx = SCREEN_WIDTH / 2;
    int spin_cy = logo_y + logo_h + gap_logo_spin + spin_box / 2;

    int text_w = str_len(COPYRIGHT_TEXT) * 8;
    int text_x = (SCREEN_WIDTH - text_w) / 2;
    int text_y = spin_cy + spin_box / 2 + gap_spin_text;

    display_fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0x00000000);
    draw_logo(logo_x, logo_y, scale);
    draw_string(text_x, text_y, COPYRIGHT_TEXT, 0x00B8B8B8);
    draw_spinner(spin_cx, spin_cy, spin_frame);
    display_blit();

    int sx = spin_cx - SPIN_RADIUS - SPIN_DOT_SIZE;
    int sy = spin_cy - SPIN_RADIUS - SPIN_DOT_SIZE;
    int sw = 2 * (SPIN_RADIUS + SPIN_DOT_SIZE);
    int sh = 2 * (SPIN_RADIUS + SPIN_DOT_SIZE);
    for (int f = 0; f < SPIN_BURST; f++) {
        spin_frame++;
        display_fill_rect(sx, sy, sw, sh, 0x00000000);
        draw_spinner(spin_cx, spin_cy, spin_frame);
        display_blit_rect(sx, sy, sw, sh);
        for (volatile int d = 0; d < SPIN_DELAY; d++) __asm__ volatile ("pause");
    }
}

void startscreen_stop(void) {
    if (!splash_active) return;
    splash_active = 0;
    display_clear(COLOR_DESKTOP);
}

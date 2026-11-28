#include "fbcon.h"
#include "font_8x8.h"
#include "virtio_gpu.h"

#define GLYPH_W 8u
#define GLYPH_H 8u
#define SCALE   2u
#define CELL_W  (GLYPH_W * SCALE)
#define CELL_H  (GLYPH_H * SCALE)

static uint32_t* framebuffer;
static uint32_t width, height;
static uint32_t columns, rows;
static uint32_t cursor_x, cursor_y;
static uint32_t foreground = 0x00E8F0F8u;
static uint32_t background = 0x00101828u;

void fbcon_init(uint32_t* buffer, uint32_t w, uint32_t h) {
    framebuffer = buffer;
    width = w;
    height = h;
    columns = w / CELL_W;
    rows = h / CELL_H;
    cursor_x = cursor_y = 0;
}

void fbcon_clear(uint32_t color) {
    if (!framebuffer) return;
    for (uint32_t i = 0; i < width * height; i++)
        framebuffer[i] = color;
    cursor_x = cursor_y = 0;
}

void fbcon_color(uint32_t fg, uint32_t bg) {
    foreground = fg;
    background = bg;
}

static void draw_glyph(char c) {
    const uint8_t* glyph = font_8x8[(uint8_t)c & 0x7Fu];
    uint32_t base_x = cursor_x * CELL_W;
    uint32_t base_y = cursor_y * CELL_H;
    for (uint32_t gy = 0; gy < GLYPH_H; gy++) {
        uint8_t bits = glyph[gy];
        for (uint32_t gx = 0; gx < GLYPH_W; gx++) {
            uint32_t color = (bits & (0x80u >> gx)) ? foreground : background;
            for (uint32_t sy = 0; sy < SCALE; sy++)
                for (uint32_t sx = 0; sx < SCALE; sx++)
                    framebuffer[(base_y + gy * SCALE + sy) * width +
                                base_x + gx * SCALE + sx] = color;
        }
    }
}

static void scroll(void) {
    for (uint32_t y = 0; y + CELL_H < height; y++)
        for (uint32_t x = 0; x < width; x++)
            framebuffer[y * width + x] = framebuffer[(y + CELL_H) * width + x];
    for (uint32_t y = height - CELL_H; y < height; y++)
        for (uint32_t x = 0; x < width; x++)
            framebuffer[y * width + x] = background;
}

void fbcon_putc(char c) {
    if (!framebuffer) return;
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\b') {
        if (cursor_x) cursor_x--;
        draw_glyph(' ');
    } else {
        draw_glyph(c);
        if (++cursor_x >= columns) {
            cursor_x = 0;
            cursor_y++;
        }
    }
    if (cursor_y >= rows) {
        scroll();
        cursor_y = rows - 1;
    }
}

void fbcon_write(const char* text) {
    while (*text) fbcon_putc(*text++);
}

void fbcon_flush(void) {
    virtio_gpu_present();
}

#include "../../mutex.h"
#include "font.h"
#include "../display.h"
#include "../../../system/graphics/fonts/font_8x8.h"



static text_rect_t text_bounds;
static uint8_t text_bounds_active;

text_rect_t font_text_bounds(void) {
    return text_bounds_active ? text_bounds : (text_rect_t){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
}
font_text_scope_t font_text_scope_begin(text_rect_t bounds) {
    font_text_scope_t previous = {text_bounds, text_bounds_active};
    text_bounds = bounds; text_bounds_active = 1;
    return previous;
}
void font_text_scope_end(font_text_scope_t previous) {
    text_bounds = previous.bounds; text_bounds_active = previous.active;
}

void draw_char(int32_t x, int32_t y, char c, uint32_t color, uint8_t bg, uint32_t bg_color) {
    MUTEX_GUARD(kernel_mutex);
    if ((uint8_t)c > 127) c = '?';
    uint32_t* buf = display_get_backbuffer();
    /* Clip only pixel writes: layout must not depend on the damaged area. */
    text_rect_t bounds = text_rect_intersect(font_text_bounds(), display_clip());
    for (int row = 0; row < 8; row++) {
        int64_t py = (int64_t)y + row;
        if (py < 0 || py >= SCREEN_HEIGHT || py < bounds.top || py >= bounds.bottom) continue;
        uint8_t row_bits = font_8x8[(uint8_t)c][row];
        for (int col = 0; col < 8; col++) {
            int64_t px = (int64_t)x + col;
            if (px < 0 || px >= SCREEN_WIDTH || px < bounds.left || px >= bounds.right) continue;
            if (row_bits & (0x80 >> col)) buf[py * SCREEN_WIDTH + px] = color;
            else if (bg) buf[py * SCREEN_WIDTH + px] = bg_color;
        }
    }
}

static void string_glyph(void* context, int32_t x, int32_t y, uint8_t ch) {
    draw_char(x, y, (char)ch, *(uint32_t*)context, 0, 0);
}
void draw_string(int32_t x, int32_t y, const char* str, uint32_t color) {
    MUTEX_GUARD(kernel_mutex);
    text_layout_draw(str, ~0u, x, y, font_text_bounds(), 8, 8, 10, string_glyph, &color);
}
void draw_string_box(int32_t x, int32_t y, const char* text, uint32_t color, text_rect_t bounds) {
    MUTEX_GUARD(kernel_mutex);
    text_rect_t parent = font_text_bounds();
    if (bounds.left < parent.left) bounds.left = parent.left;
    if (bounds.top < parent.top) bounds.top = parent.top;
    if (bounds.right > parent.right) bounds.right = parent.right;
    if (bounds.bottom > parent.bottom) bounds.bottom = parent.bottom;
    text_layout_draw(text, ~0u, x, y, bounds, 8, 8, 10, string_glyph, &color);
}

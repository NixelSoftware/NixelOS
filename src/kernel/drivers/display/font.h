#ifndef FONT_H
#define FONT_H

#include "../../types.h"
#include "../../../system/graphics/text_layout.h"

void draw_char(int32_t x, int32_t y, char c, uint32_t color, uint8_t bg, uint32_t bg_color);
void draw_string(int32_t x, int32_t y, const char* str, uint32_t color);

/* WM callback scopes are serialized by kernel_mutex; nesting restores the
 * caller's bounds, including when a callback triggers a nested redraw. */
typedef struct { text_rect_t bounds; uint8_t active; } font_text_scope_t;
font_text_scope_t font_text_scope_begin(text_rect_t bounds);
void font_text_scope_end(font_text_scope_t previous);
text_rect_t font_text_bounds(void);
void draw_string_box(int32_t x, int32_t y, const char* text, uint32_t color,
                     text_rect_t bounds);

#endif
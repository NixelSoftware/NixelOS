#ifndef SYSTEM_TEXT_LAYOUT_H
#define SYSTEM_TEXT_LAYOUT_H
#include "../../kernel/types.h"

/* Exclusive right/bottom edges, in the destination's coordinate system. */
typedef struct { int32_t left, top, right, bottom; } text_rect_t;
typedef void (*text_glyph_fn)(void* context, int32_t x, int32_t y, uint8_t ch);

/* Word wrap, hard-break long words, honor newlines/tabs, and emit only whole
 * glyphs inside bounds. Continuation lines start at the supplied x anchor. */
void text_layout_draw(const char* text, uint32_t limit, int32_t x, int32_t y,
                      text_rect_t bounds, int32_t glyph_width,
                      int32_t glyph_height, int32_t line_height,
                      text_glyph_fn glyph, void* context);
#endif

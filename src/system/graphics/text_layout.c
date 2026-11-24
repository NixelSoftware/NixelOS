#include "text_layout.h"

static int whitespace(uint8_t ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; }

void text_layout_draw(const char* text, uint32_t limit, int32_t x, int32_t y,
                      text_rect_t bounds, int32_t gw, int32_t gh, int32_t line_height,
                      text_glyph_fn glyph, void* context) {
    if (!text || !glyph || gw <= 0 || gh <= 0 || line_height < gh ||
        bounds.right <= bounds.left || bounds.bottom <= bounds.top) return;
    if (x < bounds.left) x = bounds.left;
    if (y < bounds.top) y = bounds.top;
    if (x >= bounds.right || y >= bounds.bottom ||
        gw > bounds.right - x || gh > bounds.bottom - y) return;
    int32_t anchor = x;
    uint32_t columns = (uint32_t)(bounds.right - anchor) / (uint32_t)gw;
    int word_start = 1, skip_space = 0;
    for (uint32_t i = 0; i < limit && text[i]; i++) {
        uint8_t ch = (uint8_t)text[i];
        if (ch == '\r') continue;
        if (skip_space && (ch == ' ' || ch == '\t')) continue;
        skip_space = 0;
        if (ch == '\n') {
            if (line_height > bounds.bottom - y) return;
            x = anchor; y += line_height; word_start = 1;
            if (gh > bounds.bottom - y) return;
            continue;
        }
        if (word_start && !whitespace(ch) && x > anchor) {
            uint32_t length = 0;
            while (length < limit - i && length <= columns && text[i + length] &&
                   !whitespace((uint8_t)text[i + length])) length++;
            if (length <= columns && length > (uint32_t)(bounds.right - x) / (uint32_t)gw) {
                if (line_height > bounds.bottom - y) return;
                x = anchor; y += line_height;
            }
        }
        unsigned repeat = ch == '\t' ? 4u - (unsigned)((x-anchor)/gw) % 4u : 1u;
        for (unsigned j = 0; j < repeat; j++) {
            if (gw > bounds.right - x) {
                /* Leave a trailing separator pending at the edge. The next
                 * word or explicit newline advances exactly one line. */
                if (whitespace(ch)) { skip_space = 1; break; }
                if (line_height > bounds.bottom - y) return;
                x = anchor; y += line_height;
            }
            if (gh > bounds.bottom - y) return;
            if (!whitespace(ch)) glyph(context, x, y, ch < 128 ? ch : '?');
            x += gw;
        }
        word_start = whitespace(ch);
    }
}

#ifndef FONT_H
#define FONT_H

#include "types.h"

void draw_char(int32_t x, int32_t y, char c, uint32_t color, uint8_t bg, uint32_t bg_color);
void draw_string(int32_t x, int32_t y, const char* str, uint32_t color);

#endif
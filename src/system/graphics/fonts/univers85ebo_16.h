#ifndef UNIVERS85EBO_16_H
#define UNIVERS85EBO_16_H

#include "../../../kernel/types.h"

#define UNIVERS16_START_CHAR 32
#define UNIVERS16_END_CHAR   126
#define UNIVERS16_NUM_CHARS  (UNIVERS16_END_CHAR - UNIVERS16_START_CHAR + 1)

extern const uint16_t univers85ebo_16[UNIVERS16_NUM_CHARS][16];

void font1_draw_char_16(int32_t x, int32_t y, char c, uint32_t color);
void font1_draw_string_16(int32_t x, int32_t y, const char* str, uint32_t color);

#endif
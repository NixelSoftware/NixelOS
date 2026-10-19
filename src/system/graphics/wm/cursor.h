#ifndef CURSOR_H
#define CURSOR_H

#include "../../../kernel/types.h"

void draw_cursor(int32_t x, int32_t y);
void wm_cursor_move(int32_t x, int32_t y, uint8_t fast_update);
void wm_cursor_draw(void);

#endif

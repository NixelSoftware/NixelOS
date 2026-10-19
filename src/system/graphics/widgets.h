#ifndef SYSTEM_GRAPHICS_WIDGETS_H
#define SYSTEM_GRAPHICS_WIDGETS_H

#include "../../kernel/types.h"

void graphics_draw_button(int32_t x, int32_t y, int32_t w, int32_t h,
                          const char* label, uint8_t pressed);

#endif

#ifndef WINDOW_H
#define WINDOW_H

#include "types.h"

typedef struct Window {
    int32_t x, y, w, h;
    const char* title;
    uint8_t visible;
    uint8_t focused;
    void (*draw_content)(struct Window* win);
    void (*handle_key)(struct Window* win, char c);
    void (*handle_mouse)(struct Window* win, int32_t local_x, int32_t local_y, uint8_t btn);
} Window;

void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);

#endif

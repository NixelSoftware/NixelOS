#ifndef WINDOW_H
#define WINDOW_H

#include "types.h"

typedef struct Window {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
    const char* title;
    uint8_t visible;
    uint8_t focused;
    void (*draw_content)(struct Window* win);
    void (*handle_key)(struct Window* win, char c);
} Window;

void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void draw_button(int32_t x, int32_t y, int32_t w, int32_t h, const char* label, uint8_t pressed);
void draw_window_frame(const Window* win);

#endif
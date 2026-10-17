#ifndef DESKTOP_H
#define DESKTOP_H

#include "types.h"
#include "window.h"

typedef struct {
    int32_t x;
    int32_t y;
    const char* label;
    Window* target_window;
    uint8_t icon_type; 
    uint8_t selected;
} DesktopIcon;

void desktop_init(Window* term_win, Window* note_win);
void desktop_draw(void);

uint8_t desktop_handle_click(int32_t mx, int32_t my, uint32_t current_time);

#endif
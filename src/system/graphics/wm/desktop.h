#ifndef DESKTOP_H
#define DESKTOP_H

#include "../../../kernel/os_api.h"

typedef struct {
    int32_t x;
    int32_t y;
    const char* label;
    const char* executable;
    Window* target_window;
    uint8_t icon_type; 
    uint8_t selected;
} DesktopIcon;

void desktop_init(os_api_t* api, Window* term_win, Window* note_win);
void desktop_draw(void);
void desktop_trace_api(const char* label);

uint8_t desktop_handle_click(int32_t mx, int32_t my, uint32_t current_time);

#endif

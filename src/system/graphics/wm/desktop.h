#ifndef DESKTOP_H
#define DESKTOP_H

#include "../../../kernel/os_api.h"

typedef struct {
    int32_t x;
    int32_t y;
    const char* label;
    const char* executable;
    int (*launch)(void);
    uint8_t icon_type; 
    uint8_t selected;
} DesktopIcon;

void desktop_init(os_api_t* api);
void desktop_draw(void);
void desktop_draw_overlay(void);
uint8_t desktop_mouse(int mx, int my, uint8_t left, uint8_t down, uint8_t right);
uint8_t desktop_overlay_active(void);
uint8_t desktop_key(char key);
void desktop_trace_api(const char* label);

uint8_t desktop_handle_click(int32_t mx, int32_t my, uint32_t current_time);

#endif

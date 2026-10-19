#ifndef SYSTEM_GRAPHICS_WM_H
#define SYSTEM_GRAPHICS_WM_H

#include "../../../kernel/os_api.h"

#define WM_MAX_WINDOWS 6
#define WM_TASKBAR_Y 568
#define WM_TASKBAR_HEIGHT 32

void wm_initialize(os_api_t* api, void (*invalidate)(void));
Window* wm_get_window(int index);
int wm_register_window(Window* window);
void wm_unregister_window(Window* window);
void wm_bring_to_front(Window* window);
void wm_draw_windows(void);
void wm_draw_taskbar(uint8_t start_menu_open);
void wm_poll(void);
uint8_t wm_start_menu_is_open(void);

#endif

#ifndef SYSTEM_GRAPHICS_WM_H
#define SYSTEM_GRAPHICS_WM_H

#include "../../../kernel/window.h"

#define WM_MAX_WINDOWS 6

void wm_initialize(void (*invalidate)(void));
Window* wm_get_window(int index);
int wm_register_window(Window* window);
void wm_unregister_window(Window* window);
void wm_bring_to_front(Window* window);

#endif

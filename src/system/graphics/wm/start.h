#ifndef SYSTEM_GRAPHICS_WM_START_H
#define SYSTEM_GRAPHICS_WM_START_H

#include "../../../kernel/types.h"
#include "../../../kernel/os_api.h"

#define START_MENU_X        2
#define START_MENU_W        160
#define START_BANNER_W      22
#define START_ITEM_H        24
#define START_ITEM_COUNT    5

enum {
    START_ITEM_PROGRAMS = 0,
    START_ITEM_TERMINAL,
    START_ITEM_FILES,
    START_ITEM_HELP,
    START_ITEM_SHUTDOWN
};

void start_menu_init(os_api_t* api);
void start_menu_toggle(void);
void start_menu_close(void);
uint8_t start_menu_is_open(void);

void start_menu_draw(void);
/* Returns 1 if the mouse event was handled/consumed by the Start Menu, 0 otherwise */
int start_menu_handle_mouse(int32_t mouse_x, int32_t mouse_y, uint8_t click_down);

#endif
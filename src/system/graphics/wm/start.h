#ifndef SYSTEM_GRAPHICS_WM_START_H
#define SYSTEM_GRAPHICS_WM_START_H

#include "../../../kernel/types.h"
#include "../../../kernel/os_api.h"

#define START_MENU_X        4
#define START_MENU_W        380
#define START_HEADER_H      52
#define START_FOOTER_H      42
#define START_ITEM_H        30

/* Items in left column (Apps) */
#define START_LEFT_COUNT    3
enum {
    START_ITEM_TERMINAL = 0,
    START_ITEM_FILES,
    START_ITEM_HELP
};

/* Action buttons in footer */
enum {
    START_ACTION_NONE = -1,
    START_ACTION_LOGOFF = 100,
    START_ACTION_SHUTDOWN = 101,
    START_ACTION_SETTINGS = 102,
    START_ACTION_NETWORK = 103
};

void start_menu_init(os_api_t* api);
void start_menu_toggle(void);
void start_menu_close(void);
uint8_t start_menu_is_open(void);

void start_menu_draw(void);
int start_menu_handle_mouse(int32_t mouse_x, int32_t mouse_y, uint8_t click_down);

#endif
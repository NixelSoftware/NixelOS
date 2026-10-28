#include "../../../include/os_version.h"
#include "start.h"
#include "wm.h"
#include "../terminal/terminal.h"
#include "../../../kernel/drivers/display/font.h"

static os_api_t* s_api = 0;
static uint8_t s_open = 0;
static int s_hovered = -1;

static const char* s_labels[START_ITEM_COUNT] = {
    "Programs",
    "Terminal",
    "Files",
    "Help",
    "Shut Down..."
};

static void draw_vertical_string(int32_t x, int32_t y, const char* str, uint32_t color) {
    while (*str) {
        draw_char(x, y, *str, color, 0, 0);
        y += 10;
        str++;
    }
}

void start_menu_init(os_api_t* api) {
    s_api = api;
    s_open = 0;
    s_hovered = -1;
}

void start_menu_toggle(void) {
    s_open = !s_open;
    s_hovered = -1;
}

void start_menu_close(void) {
    s_open = 0;
    s_hovered = -1;
}

uint8_t start_menu_is_open(void) {
    return s_open;
}

void start_menu_draw(void) {
    if (!s_api || !s_open) return;

    const int menu_h = (START_ITEM_COUNT * START_ITEM_H) + 10;
    const int menu_y = WM_TASKBAR_Y - menu_h;
    const int menu_x = START_MENU_X;

    s_api->graphics.fill_rect(menu_x, menu_y, START_MENU_W, menu_h, COLOR_WIN_BG);

    s_api->graphics.fill_rect(menu_x, menu_y, START_MENU_W, 2, COLOR_WHITE);
    s_api->graphics.fill_rect(menu_x, menu_y, 2, menu_h, COLOR_WHITE);
    s_api->graphics.fill_rect(menu_x + START_MENU_W - 2, menu_y, 2, menu_h, COLOR_BLACK);
    s_api->graphics.fill_rect(menu_x, menu_y + menu_h - 2, START_MENU_W, 2, COLOR_BLACK);
    s_api->graphics.fill_rect(menu_x + START_MENU_W - 3, menu_y + 1, 1, menu_h - 2, COLOR_DARK_GRAY);
    s_api->graphics.fill_rect(menu_x + 1, menu_y + menu_h - 3, START_MENU_W - 2, 1, COLOR_DARK_GRAY);

    s_api->graphics.fill_rect(menu_x + 2, menu_y + 2, START_BANNER_W, menu_h - 5, 0x00000080);
    draw_vertical_string(menu_x + 8, menu_y + 12, "NIXELOS", COLOR_WHITE);

    int item_x = menu_x + START_BANNER_W + 4;
    int item_w = START_MENU_W - START_BANNER_W - 8;
    int cur_y = menu_y + 4;

    for (int i = 0; i < START_ITEM_COUNT; i++) {
        if (i == START_ITEM_SHUTDOWN) {
            s_api->graphics.fill_rect(item_x, cur_y + 2, item_w, 1, COLOR_DARK_GRAY);
            s_api->graphics.fill_rect(item_x, cur_y + 3, item_w, 1, COLOR_WHITE);
            cur_y += 6;
        }

        uint32_t text_color = COLOR_BLACK;

        if (s_hovered == i) {
            s_api->graphics.fill_rect(item_x, cur_y, item_w, START_ITEM_H, COLOR_TITLE_BAR);
            text_color = COLOR_WHITE;
        }

        s_api->graphics.fill_rect(item_x + 3, cur_y + 5, 12, 12, (s_hovered == i) ? COLOR_WHITE : 0x00A0A0A0);

        s_api->graphics.draw_string(item_x + 20, cur_y + 7, s_labels[i], text_color);

        if (i == START_ITEM_PROGRAMS) {
            s_api->graphics.draw_string(menu_x + START_MENU_W - 14, cur_y + 7, ">", text_color);
        }

        cur_y += START_ITEM_H;
    }
}

int start_menu_handle_mouse(int32_t mouse_x, int32_t mouse_y, uint8_t click_down) {
    if (!s_open) return 0;

    const int menu_h = (START_ITEM_COUNT * START_ITEM_H) + 10;
    const int menu_y = WM_TASKBAR_Y - menu_h;
    const int menu_x = START_MENU_X;
    const int item_x = menu_x + START_BANNER_W + 4;
    const int item_w = START_MENU_W - START_BANNER_W - 8;

    int prev_hover = s_hovered;
    s_hovered = -1;

    if (mouse_x >= item_x && mouse_x <= (item_x + item_w)) {
        int check_y = menu_y + 4;
        for (int i = 0; i < START_ITEM_COUNT; i++) {
            if (i == START_ITEM_SHUTDOWN) check_y += 6;
            if (mouse_y >= check_y && mouse_y < check_y + START_ITEM_H) {
                s_hovered = i;
                break;
            }
            check_y += START_ITEM_H;
        }
    }

    if (prev_hover != s_hovered) {
        request_redraw();
    }

    if (click_down) {
        if (s_hovered >= 0) {
            int selected = s_hovered;
            start_menu_close();
            request_redraw();

            switch (selected) {
                case START_ITEM_PROGRAMS:
                case START_ITEM_FILES:
                    if (s_api && s_api->shell.launch_app) s_api->shell.launch_app("files.bin", "");
                    break;
                case START_ITEM_TERMINAL:
                    if (terminal_launch() < 0 && s_api && s_api->windows.show_info_dialog)
                        s_api->windows.show_info_dialog("Cannot launch", "No free terminal, window, or process slots.");
                    break;
                case START_ITEM_HELP:
                    if (s_api && s_api->windows.show_info_dialog) {
                        s_api->windows.show_info_dialog("Help", OS_NAME " Workstation\nVersion " OS_VERSION "\nBuild " OS_BUILD_ID "\nBuilt on x86 Architecture");
                    }
                    break;
                case START_ITEM_SHUTDOWN:
                    if (s_api && s_api->shell.launch_app) {
                        s_api->shell.launch_app("shutdlg.bin", "");
                    }
                    break;
            }
            return 1;
        }

        if (mouse_x >= menu_x && mouse_x <= menu_x + START_MENU_W &&
            mouse_y >= menu_y && mouse_y <= menu_y + menu_h) {
            return 1;
        }

        start_menu_close();
        request_redraw();
        return 0;
    }

    return 0;
}
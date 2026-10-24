#include "../../kernel/types.h"
#include "../../kernel/window.h"
#include "../../system/user/sys_api.h"

#define COLOR_WIN_BG     0x00C0C0C0
#define COLOR_DARK_GRAY  0x00808080
#define COLOR_WHITE      0x00FFFFFF
#define COLOR_BLACK      0x00000000
#define COLOR_BLUE       0x00000080

enum {
    ACTION_SHUTDOWN = 0,
    ACTION_RESTART  = 1
};

static Window shut_win;
static uint32_t shut_win_handle;
static uint8_t shut_close_requested;
static int selected_action = ACTION_SHUTDOWN;

static int user_draw(uint32_t op, int32_t x, int32_t y, int32_t w, int32_t h,
                     const char* text, uint32_t color, uint8_t pressed) {
    user_window_draw_t d = {0};
    d.operation = op;
    d.x = x - shut_win.x;
    d.y = y - shut_win.y;
    d.width = w;
    d.height = h;
    d.text = text;
    d.color = color;
    d.pressed = pressed;
    return sys_window_draw(shut_win_handle, &d);
}

static void shut_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t col) {
    user_draw(USER_WINDOW_DRAW_FILL, x, y, w, h, 0, col, 0);
}

static void draw_text(int32_t x, int32_t y, const char* str, uint32_t col) {
    user_draw(USER_WINDOW_DRAW_TEXT, x, y, 0, 0, str, col, 0);
}

static void draw_btn(int32_t x, int32_t y, int32_t w, int32_t h, const char* lbl, uint8_t pressed) {
    user_draw(USER_WINDOW_DRAW_BUTTON, x, y, w, h, lbl, COLOR_WIN_BG, pressed);
}

static void draw_radio_button(int32_t x, int32_t y, const char* label, uint8_t selected) {
    shut_fill_rect(x, y, 12, 12, COLOR_WHITE);
    shut_fill_rect(x, y, 12, 1, COLOR_DARK_GRAY);
    shut_fill_rect(x, y, 1, 12, COLOR_DARK_GRAY);
    shut_fill_rect(x, y + 11, 12, 1, COLOR_WHITE);
    shut_fill_rect(x + 11, y, 1, 12, COLOR_WHITE);

    if (selected) {
        shut_fill_rect(x + 4, y + 4, 4, 4, COLOR_BLACK);
    }
    draw_text(x + 18, y + 2, label, COLOR_BLACK);
}

static void draw_computer_icon(int32_t x, int32_t y) {
    shut_fill_rect(x + 2, y + 2, 28, 20, COLOR_DARK_GRAY);
    shut_fill_rect(x + 4, y + 4, 24, 16, 0x00008080);
    shut_fill_rect(x + 12, y + 22, 8, 5, COLOR_DARK_GRAY);
    shut_fill_rect(x + 8, y + 27, 16, 3, COLOR_DARK_GRAY);
}

static void shut_draw_content(Window* win) {
    shut_fill_rect(win->x + 2, win->y + 20, win->w - 4, win->h - 22, COLOR_WIN_BG);

    draw_computer_icon(win->x + 16, win->y + 36);
    draw_text(win->x + 58, win->y + 34, "What do you want the computer to do?", COLOR_BLACK);

    int32_t opt_x = win->x + 60;
    int32_t opt_y1 = win->y + 60;
    int32_t opt_y2 = win->y + 82;

    draw_radio_button(opt_x, opt_y1, "Shut down the computer?", selected_action == ACTION_SHUTDOWN);
    draw_radio_button(opt_x, opt_y2, "Restart the computer?", selected_action == ACTION_RESTART);

    int32_t btn_y = win->y + win->h - 32;
    draw_btn(win->x + 50,  btn_y, 65, 22, "Yes", 0);
    draw_btn(win->x + 125, btn_y, 65, 22, "No", 0);
    draw_btn(win->x + 200, btn_y, 65, 22, "Cancel", 0);
}

static void shut_handle_mouse(Window* win, int32_t lx, int32_t ly, uint8_t btn) {
    if (btn != 1) return;

    if (lx >= 60 && lx <= 280 && ly >= 60 && ly <= 75) {
        selected_action = ACTION_SHUTDOWN;
        shut_draw_content(win);
        sys_window_present(shut_win_handle);
        return;
    }

    if (lx >= 60 && lx <= 280 && ly >= 82 && ly <= 97) {
        selected_action = ACTION_RESTART;
        shut_draw_content(win);
        sys_window_present(shut_win_handle);
        return;
    }

    int32_t btn_ly = win->h - 32;
    if (ly >= btn_ly && ly <= btn_ly + 22) {
        if (lx >= 50 && lx <= 115) {
            if (selected_action == ACTION_SHUTDOWN) {
                sys_shutdown();
            } else {
                sys_reboot();
            }
            return;
        }
        if ((lx >= 125 && lx <= 190) || (lx >= 200 && lx <= 265)) {
            shut_close_requested = 1;
            return;
        }
    }
}

static void shut_handle_key(Window* win, char c) {
    (void)win;
    if ((uint8_t)c == 0x01 || c == 27) {
        shut_close_requested = 1;
        return;
    }
    if (c == '\n' || c == '\r') {
        if (selected_action == ACTION_SHUTDOWN) {
            sys_shutdown();
        } else {
            sys_reboot();
        }
        return;
    }
    if ((uint8_t)c == 0x48) {
        selected_action = ACTION_SHUTDOWN;
        shut_draw_content(win);
        sys_window_present(shut_win_handle);
    } else if ((uint8_t)c == 0x50) {
        selected_action = ACTION_RESTART;
        shut_draw_content(win);
        sys_window_present(shut_win_handle);
    }
}

void user_main(const char* arguments) {
    (void)arguments;
    shut_win_handle = 0;
    shut_close_requested = 0;

    shut_win.w = 320;
    shut_win.h = 160;
    shut_win.x = (SCREEN_WIDTH - shut_win.w) / 2;
    shut_win.y = (SCREEN_HEIGHT - 32 - shut_win.h) / 2;
    shut_win.title = "Shut Down NixelOS";

    user_window_desc_t desc = {0};
    desc.x = shut_win.x;
    desc.y = shut_win.y;
    desc.width = shut_win.w;
    desc.height = shut_win.h;

    const char* t = shut_win.title;
    int i = 0;
    while (t[i] && i < USER_WINDOW_TITLE_MAX - 1) {
        desc.title[i] = t[i];
        i++;
    }
    desc.title[i] = '\0';

    int32_t handle = sys_window_create(&desc);
    if (handle < 0) return;
    shut_win_handle = (uint32_t)handle;

    shut_draw_content(&shut_win);
    sys_window_present(shut_win_handle);

    while (!shut_close_requested) {
        user_window_event_t event;
        sys_yield();
        while (sys_window_poll(shut_win_handle, &event) == 1) {
            if (event.type == USER_WINDOW_EVENT_CLOSE) {
                shut_close_requested = 1;
                break;
            }
            if (event.type == USER_WINDOW_EVENT_REDRAW) {
                shut_win.x = event.x;
                shut_win.y = event.y;
                shut_draw_content(&shut_win);
                sys_window_present(shut_win_handle);
            } else if (event.type == USER_WINDOW_EVENT_KEY) {
                shut_handle_key(&shut_win, (char)event.key);
            } else if (event.type == USER_WINDOW_EVENT_MOUSE) {
                shut_handle_mouse(&shut_win, event.x, event.y, (uint8_t)event.buttons);
            }
            if (shut_close_requested) break;
        }
    }

    sys_window_close(shut_win_handle);
}
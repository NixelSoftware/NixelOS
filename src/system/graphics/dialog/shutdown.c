#include "shutdown.h"
#include "../wm/theme.h"
#include "../wm/wm.h"
#include "../../../kernel/drivers/power.h"

enum {
    ACTION_SHUTDOWN = 0,
    ACTION_RESTART  = 1
};

static Window shut_win;
static os_api_t* os;
static int selected_action = ACTION_SHUTDOWN;

static void shut_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t col) {
    os->graphics.fill_rect(x, y, w, h, col);
}

static void draw_text(int32_t x, int32_t y, const char* text, uint32_t col) {
    os->graphics.draw_string(x, y, text, col);
}

static void draw_btn(int32_t x, int32_t y, int32_t w, int32_t h, const char* text, uint8_t pressed) {
    os->graphics.draw_button(x, y, w, h, text, pressed);
}

static void shut_close(void) {
    shut_win.visible = shut_win.focused = 0;
    os->windows.unregister_window(&shut_win);
}

static void draw_radio_button(int32_t x, int32_t y, const char* label, uint8_t selected) {
    shut_fill_rect(x, y, 12, 12, COLOR_MODERN_BG);
    shut_fill_rect(x, y, 12, 1, COLOR_SURFACE_BORDER);
    shut_fill_rect(x, y, 1, 12, COLOR_SURFACE_BORDER);
    shut_fill_rect(x, y + 11, 12, 1, COLOR_SURFACE_BORDER);
    shut_fill_rect(x + 11, y, 1, 12, COLOR_SURFACE_BORDER);

    if (selected) {
        shut_fill_rect(x + 4, y + 4, 4, 4, COLOR_TEXT_PRIMARY);
    }
    draw_text(x + 18, y + 2, label, COLOR_TEXT_PRIMARY);
}

static void draw_computer_icon(int32_t x, int32_t y) {
    shut_fill_rect(x + 2, y + 2, 28, 20, COLOR_SURFACE_BORDER);
    shut_fill_rect(x + 4, y + 4, 24, 16, COLOR_ACCENT);
    shut_fill_rect(x + 12, y + 22, 8, 5, COLOR_SURFACE_BORDER);
    shut_fill_rect(x + 8, y + 27, 16, 3, COLOR_SURFACE_BORDER);
}

static void shut_draw_content(Window* win) {
    shut_fill_rect(win->x + 2, win->y + 22, win->w - 4, win->h - 24, COLOR_SURFACE);

    draw_computer_icon(win->x + 16, win->y + 36);
    draw_text(win->x + 58, win->y + 34, "What do you want the computer to do?", COLOR_TEXT_PRIMARY);

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

/* This component is linked into the kernel. No disk executable, process
 * elevation, or user-provided callback participates in this trusted UI action. */
static void request_power(void) {
    if (selected_action == ACTION_SHUTDOWN) power_shutdown();
    else power_reboot();
}

static void shut_handle_mouse(Window* win, int32_t lx, int32_t ly, uint8_t btn) {
    if (btn != 1) return;

    if (lx >= 60 && lx <= 280 && ly >= 60 && ly <= 75) {
        selected_action = ACTION_SHUTDOWN;
        request_redraw();
        return;
    }

    if (lx >= 60 && lx <= 280 && ly >= 82 && ly <= 97) {
        selected_action = ACTION_RESTART;
        request_redraw();
        return;
    }

    int32_t btn_ly = win->h - 32;
    if (ly >= btn_ly && ly <= btn_ly + 22) {
        if (lx >= 50 && lx <= 115) {
            request_power();
            return;
        }
        if ((lx >= 125 && lx <= 190) || (lx >= 200 && lx <= 265)) {
            shut_close();
            return;
        }
    }
}

static void shut_handle_key(Window* win, char c) {
    (void)win;
    if ((uint8_t)c == 0x01 || c == 27) {
        shut_close();
        return;
    }
    if (c == '\n' || c == '\r') {
        request_power();
        return;
    }
    if ((uint8_t)c == 0x48) {
        selected_action = ACTION_SHUTDOWN;
        request_redraw();
    } else if ((uint8_t)c == 0x50) {
        selected_action = ACTION_RESTART;
        request_redraw();
    }
}

void shutdown_dialog_init(os_api_t* api) {
    os = api;
}

int shutdown_dialog_open(void) {
    if (!os) return -1;
    selected_action = ACTION_SHUTDOWN;
    shut_win.w = 320;
    shut_win.h = 160;
    shut_win.x = (SCREEN_WIDTH - shut_win.w) / 2;
    shut_win.y = (SCREEN_HEIGHT - 32 - shut_win.h) / 2;
    shut_win.title = "Shut Down NixelOS";
    shut_win.draw_content = shut_draw_content;
    shut_win.handle_key = shut_handle_key;
    shut_win.handle_mouse = shut_handle_mouse;
    return os->windows.register_window(&shut_win);
}

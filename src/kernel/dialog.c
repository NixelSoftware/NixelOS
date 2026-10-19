#include "dialog.h"
#include "dialog_icon.h"
#include "window.h"
#include "os_api.h"

#define COLOR_WIN_BG     0x00C0C0C0
#define COLOR_DARK_GRAY  0x00808080
#define COLOR_WHITE      0x00FFFFFF
#define COLOR_BLACK      0x00000000

static os_api_t* os = 0;
static Window dialog_window;
static char dialog_title_buf[32];
static char dialog_msg_buf[128];
static int dialog_active = 0;

void dialog_init(os_api_t* api) {
    os = api;
}

static void str_copy(char* dest, const char* src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static void draw_dialog_button(int32_t x, int32_t y, int32_t w, int32_t h, const char* label) {
    if (!os) return;
    os->graphics.fill_rect(x, y, w, h, COLOR_WIN_BG);
    os->graphics.fill_rect(x, y, w, 1, COLOR_WHITE);
    os->graphics.fill_rect(x, y, 1, h, COLOR_WHITE);
    os->graphics.fill_rect(x, y + h - 1, w, 1, COLOR_DARK_GRAY);
    os->graphics.fill_rect(x + w - 1, y, 1, h, COLOR_DARK_GRAY);
    os->graphics.draw_string(x + (w / 2) - 8, y + 4, label, COLOR_BLACK);
}

static void dialog_draw_content(Window* win) {
    if (!os) return;

    int32_t icon_x = win->x + 14;
    int32_t icon_y = win->y + 32;

    for (int r = 0; r < 16; r++) {
        for (int c = 0; c < 16; c++) {
            uint32_t pixel = icon_info_16x16[r * 16 + c];
            if (pixel != ICON_TRANS) {
                os->graphics.fill_rect(icon_x + c, icon_y + r, 1, 1, pixel);
            }
        }
    }

    os->graphics.draw_string(win->x + 40, win->y + 34, dialog_msg_buf, COLOR_BLACK);

    int32_t btn_w = 60;
    int32_t btn_h = 22;
    int32_t btn_x = win->x + (win->w / 2) - (btn_w / 2);
    int32_t btn_y = win->y + win->h - 30;
    draw_dialog_button(btn_x, btn_y, btn_w, btn_h, "OK");
}

static void dialog_close(void) {
    if (!os) return;
    dialog_window.visible = 0;
    dialog_window.focused = 0;
    dialog_active = 0;
    os->windows.unregister_window(&dialog_window);
}

static void dialog_handle_mouse(Window* win, int32_t lx, int32_t ly, uint8_t btn) {
    (void)btn;
    int32_t btn_w = 60;
    int32_t btn_h = 22;
    int32_t btn_lx = (win->w / 2) - (btn_w / 2);
    int32_t btn_ly = win->h - 30;

    if (lx >= btn_lx && lx <= btn_lx + btn_w &&
        ly >= btn_ly && ly <= btn_ly + btn_h) 
    {
        dialog_close();
    }
}

static void dialog_handle_key(Window* win, char c) {
    (void)win;
    if (c == '\n' || c == ' ' || (uint8_t)c == 0x01 || (uint8_t)c == 0x1C) {
        dialog_close();
    }
}

void show_info_dialog(const char* title, const char* message) {
    if (!os) return;
    str_copy(dialog_title_buf, title ? title : "Information", 32);
    str_copy(dialog_msg_buf, message ? message : "", 128);

    int msg_len = 0;
    while (dialog_msg_buf[msg_len]) msg_len++;
    int calc_w = 40 + (msg_len * 8) + 24;
    if (calc_w < 180) calc_w = 180;
    if (calc_w > 400) calc_w = 400;

    dialog_window.w = calc_w;
    dialog_window.h = 100;
    dialog_window.x = (640 / 2) - (dialog_window.w / 2);
    dialog_window.y = (480 / 2) - (dialog_window.h / 2);
    dialog_window.title = dialog_title_buf;
    dialog_window.visible = 1;
    dialog_window.focused = 1;
    dialog_window.draw_content = dialog_draw_content;
    dialog_window.handle_mouse = dialog_handle_mouse;
    dialog_window.handle_key = dialog_handle_key;

    os->windows.register_window(&dialog_window);
    dialog_active = 1;
}
#include "crash_dialog.h"
#include "../wm/theme.h"
#include "../wm/wm.h"
#include "../../../kernel/window.h"
#include "../../../kernel/os_api.h"

#define ICON_TRANS 0x00FF00FF
#define ICON_RED   0x00E03131
#define ICON_WHITE 0x00FFFFFF

/* 16x16 Classic Red Circle (X) Error Icon */
static const uint32_t icon_error_16x16[256] = {
    ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS,
    ICON_TRANS, ICON_TRANS, ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS,
    ICON_TRANS, ICON_RED,   ICON_RED,   ICON_RED,   ICON_WHITE, ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_WHITE, ICON_RED,   ICON_RED,   ICON_RED,   ICON_TRANS, ICON_TRANS, ICON_TRANS,
    ICON_TRANS, ICON_RED,   ICON_RED,   ICON_WHITE, ICON_WHITE, ICON_WHITE, ICON_RED,   ICON_RED,   ICON_WHITE, ICON_WHITE, ICON_WHITE, ICON_RED,   ICON_RED,   ICON_TRANS, ICON_TRANS, ICON_TRANS,
    ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_WHITE, ICON_WHITE, ICON_WHITE, ICON_WHITE, ICON_WHITE, ICON_WHITE, ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_TRANS, ICON_TRANS,
    ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_WHITE, ICON_WHITE, ICON_WHITE, ICON_WHITE, ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_TRANS, ICON_TRANS,
    ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_WHITE, ICON_WHITE, ICON_WHITE, ICON_WHITE, ICON_WHITE, ICON_WHITE, ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_TRANS, ICON_TRANS,
    ICON_RED,   ICON_RED,   ICON_WHITE, ICON_WHITE, ICON_WHITE, ICON_RED,   ICON_WHITE, ICON_WHITE, ICON_RED,   ICON_WHITE, ICON_WHITE, ICON_WHITE, ICON_RED,   ICON_RED,   ICON_TRANS, ICON_TRANS,
    ICON_RED,   ICON_RED,   ICON_WHITE, ICON_WHITE, ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_WHITE, ICON_WHITE, ICON_RED,   ICON_RED,   ICON_TRANS, ICON_TRANS,
    ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_TRANS, ICON_TRANS,
    ICON_TRANS, ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_TRANS, ICON_TRANS, ICON_TRANS,
    ICON_TRANS, ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_TRANS, ICON_TRANS, ICON_TRANS,
    ICON_TRANS, ICON_TRANS, ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS,
    ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_RED,   ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS,
    ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS,
    ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS, ICON_TRANS
};

static os_api_t* os = 0;
static Window crash_win;
static char line_prog[64];
static char line_fault[64];
static char line_reg[64];

static void hex_to_str(char* out, uint32_t val) {
    const char hex_chars[] = "0123456789ABCDEF";
    out[0] = '0'; out[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        out[2 + (7 - i)] = hex_chars[(val >> (i * 4)) & 0x0F];
    }
    out[10] = '\0';
}

static const char* get_fault_name(uint32_t vector) {
    switch (vector) {
        case 0:  return "Division By Zero (#DE)";
        case 6:  return "Invalid Opcode (#UD)";
        case 13: return "General Protection Fault (#GP)";
        case 14: return "Page Fault (#PF)";
        default: return "Fatal Hardware Exception";
    }
}

static void crash_draw_content(Window* win) {
    if (!os) return;

    /* Window client background */
    os->graphics.fill_rect(win->x + 2, win->y + 22, win->w - 4, win->h - 24, COLOR_SURFACE);

    /* Error Icon */
    int32_t icon_x = win->x + 16;
    int32_t icon_y = win->y + 36;
    for (int r = 0; r < 16; r++) {
        for (int c = 0; c < 16; c++) {
            uint32_t pixel = icon_error_16x16[r * 16 + c];
            if (pixel != ICON_TRANS) {
                os->graphics.fill_rect(icon_x + c, icon_y + r, 1, 1, pixel);
            }
        }
    }

    /* Diagnostics info */
    os->graphics.draw_string(win->x + 44, win->y + 34, line_prog, COLOR_TEXT_PRIMARY);
    os->graphics.draw_string(win->x + 44, win->y + 52, line_fault, COLOR_BTN_CLOSE);
    os->graphics.draw_string(win->x + 44, win->y + 70, line_reg, COLOR_TEXT_MUTED);

    /* Close Button */
    int32_t btn_w = 75;
    int32_t btn_h = 24;
    int32_t btn_x = win->x + win->w - btn_w - 16;
    int32_t btn_y = win->y + win->h - 34;

    os->graphics.draw_button(btn_x, btn_y, btn_w, btn_h, "Close", 0);
}

static void crash_close(void) {
    if (!os) return;
    crash_win.visible = 0;
    crash_win.focused = 0;
    os->windows.unregister_window(&crash_win);
    request_redraw();
}

static void crash_handle_mouse(Window* win, int32_t lx, int32_t ly, uint8_t btn) {
    (void)btn;
    int32_t btn_w = 75;
    int32_t btn_h = 24;
    int32_t btn_lx = win->w - btn_w - 16;
    int32_t btn_ly = win->h - 34;

    if (lx >= btn_lx && lx <= btn_lx + btn_w &&
        ly >= btn_ly && ly <= btn_ly + btn_h) {
        crash_close();
    }
}

static void crash_handle_key(Window* win, char c) {
    (void)win;
    if (c == '\n' || c == ' ' || (uint8_t)c == 0x01 || (uint8_t)c == 0x1C) {
        crash_close();
    }
}

void crash_dialog_init(os_api_t* api) {
    os = api;
}

void crash_dialog_show(const char* app_name, uint32_t vector, uint32_t eip, uint32_t err_code) {
    if (!os) return;

    int p = 0;
    const char* pfx = "Illegal operation has occurred";
    while (*pfx) line_prog[p++] = *pfx++;
    //const char* an = (app_name && *app_name) ? app_name : "Application";
    //while (*an && p < 45) line_prog[p++] = *an++;
    //line_prog[p] = '\0';

    p = 0;
    const char* flt = get_fault_name(vector);
    while (*flt) line_fault[p++] = *flt++;
    line_fault[p] = '\0';

    p = 0;
    const char* at = "at EIP=";
    while (*at) line_reg[p++] = *at++;
    char hex_buf[12];
    hex_to_str(hex_buf, eip);
    for (int i = 0; i < 10; i++) line_reg[p++] = hex_buf[i];
    const char* code_str = " Code=";
    while (*code_str) line_reg[p++] = *code_str++;
    hex_to_str(hex_buf, err_code);
    for (int i = 0; i < 10; i++) line_reg[p++] = hex_buf[i];
    line_reg[p] = '\0';

    crash_win.w = 380;
    crash_win.h = 140;
    crash_win.x = (SCREEN_WIDTH - crash_win.w) / 2;
    crash_win.y = (SCREEN_HEIGHT - crash_win.h) / 2;
    crash_win.title = "Illegal Operation";
    crash_win.visible = 1;
    crash_win.focused = 1;
    crash_win.draw_content = crash_draw_content;
    crash_win.handle_mouse = crash_handle_mouse;
    crash_win.handle_key = crash_handle_key;

    os->windows.register_window(&crash_win);
    request_redraw();
}
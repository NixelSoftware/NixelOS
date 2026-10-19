#include "../../kernel/types.h"
#include "../../kernel/os_api.h"
#include "../../kernel/window.h"

#define COLOR_WIN_BG      0x00C0C0C0
#define COLOR_DARK_GRAY   0x00808080
#define COLOR_LIGHT_GRAY  0x00DFDFDF
#define COLOR_WHITE       0x00FFFFFF
#define COLOR_BLACK       0x00000000
#define COLOR_BLUE        0x000000AA

#define BTN_W 36
#define BTN_H 26
#define BTN_GAP 6

static os_api_t* os = 0;
static Window calc_window;

static char display_buf[24] = "0";
static int  display_len = 1;
static int32_t accumulator = 0;
static char pending_op = 0;
static uint8_t clear_on_next_digit = 0;

static const char* button_labels[4][4] = {
    { "7", "8", "9", "/" },
    { "4", "5", "6", "*" },
    { "1", "2", "3", "-" },
    { "C", "0", "=", "+" }
};

static void int_to_str(int32_t val, char* buf) {
    char temp[16];
    int i = 0, j = 0;
    int is_neg = 0;

    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    if (val < 0) {
        is_neg = 1;
        val = -val;
    }

    while (val > 0 && i < 15) {
        temp[i++] = (char)('0' + (val % 10));
        val /= 10;
    }

    if (is_neg) {
        buf[j++] = '-';
    }

    while (i > 0) {
        buf[j++] = temp[--i];
    }
    buf[j] = '\0';
}

static int32_t str_to_int(const char* buf) {
    int32_t res = 0;
    int is_neg = 0;
    int i = 0;

    if (buf[0] == '-') {
        is_neg = 1;
        i = 1;
    }

    while (buf[i] >= '0' && buf[i] <= '9') {
        res = res * 10 + (buf[i] - '0');
        i++;
    }

    return is_neg ? -res : res;
}

static void apply_op(void) {
    int32_t current = str_to_int(display_buf);

    if (pending_op == '+') accumulator += current;
    else if (pending_op == '-') accumulator -= current;
    else if (pending_op == '*') accumulator *= current;
    else if (pending_op == '/') {
        if (current != 0) accumulator /= current;
        else accumulator = 0; 
    } else {
        accumulator = current;
    }

    int_to_str(accumulator, display_buf);
    display_len = 0;
    while (display_buf[display_len]) display_len++;
}

static void handle_input(char key) {
    if (key >= '0' && key <= '9') {
        if (clear_on_next_digit || (display_len == 1 && display_buf[0] == '0')) {
            display_buf[0] = key;
            display_buf[1] = '\0';
            display_len = 1;
            clear_on_next_digit = 0;
        } else if (display_len < 12) {
            display_buf[display_len++] = key;
            display_buf[display_len] = '\0';
        }
        return;
    }

    if (key == 'C' || key == 'c') {
        display_buf[0] = '0';
        display_buf[1] = '\0';
        display_len = 1;
        accumulator = 0;
        pending_op = 0;
        clear_on_next_digit = 0;
        return;
    }

    if (key == '+' || key == '-' || key == '*' || key == '/') {
        apply_op();
        pending_op = key;
        clear_on_next_digit = 1;
        return;
    }

    if (key == '=' || key == '\n') {
        apply_op();
        pending_op = 0;
        clear_on_next_digit = 1;
        return;
    }
}

static void draw_sunken_panel(int32_t x, int32_t y, int32_t w, int32_t h) {
    os->graphics.fill_rect(x, y, w, h, COLOR_WHITE);
    os->graphics.fill_rect(x - 1, y - 1, w + 2, 1, COLOR_DARK_GRAY);
    os->graphics.fill_rect(x - 1, y - 1, 1, h + 2, COLOR_DARK_GRAY);
    os->graphics.fill_rect(x - 1, y + h, w + 2, 1, COLOR_WHITE);
    os->graphics.fill_rect(x + w, y - 1, 1, h + 2, COLOR_WHITE);
}

void calc_draw_content(Window* win) {
    int32_t disp_x = win->x + 12;
    int32_t disp_y = win->y + 26;
    int32_t disp_w = win->w - 24;
    int32_t disp_h = 24;
    draw_sunken_panel(disp_x, disp_y, disp_w, disp_h);

    int text_x = disp_x + disp_w - 10 - (display_len * 8);
    if (text_x < disp_x + 6) text_x = disp_x + 6;
    os->graphics.draw_string(text_x, disp_y + 6, display_buf, COLOR_BLACK);

    int32_t grid_top_y = disp_y + disp_h + 10;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int32_t bx = disp_x + c * (BTN_W + BTN_GAP);
            int32_t by = grid_top_y + r * (BTN_H + BTN_GAP);
            os->graphics.draw_button(bx, by, BTN_W, BTN_H, button_labels[r][c], 0);
        }
    }
}

void calc_handle_mouse(Window* win, int32_t lx, int32_t ly, uint8_t btn) {
    (void)btn;
    int32_t disp_x = 12;
    int32_t grid_top_y = 26 + 24 + 10;

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int32_t bx = disp_x + c * (BTN_W + BTN_GAP);
            int32_t by = grid_top_y + r * (BTN_H + BTN_GAP);

            if (lx >= bx && lx < bx + BTN_W && ly >= by && ly < by + BTN_H) {
                handle_input(button_labels[r][c][0]);
                calc_draw_content(win);
                os->graphics.blit_all();
                return;
            }
        }
    }
}

void calc_handle_key(Window* win, char c) {
    if ((uint8_t)c == 0x01) {
        os->windows.unregister_window(win);
        return;
    }

    if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '*' || c == '/' || c == '=' || c == '\n' || c == 'c' || c == 'C') {
        handle_input(c);
        calc_draw_content(win);
        os->graphics.blit_all();
    }
}

int main_app(os_api_t* api) {
    if (!api) return -1;
    os = api;

    display_buf[0] = '0';
    display_buf[1] = '\0';
    display_len = 1;
    accumulator = 0;
    pending_op = 0;
    clear_on_next_digit = 0;

    calc_window.x = 240;
    calc_window.y = 120;
    calc_window.w = 188;
    calc_window.h = 196;
    calc_window.title = "Calculator";
    calc_window.visible = 1;
    calc_window.focused = 1;
    calc_window.draw_content = calc_draw_content;
    calc_window.handle_key = calc_handle_key;
    calc_window.handle_mouse = calc_handle_mouse;

    os->windows.register_window(&calc_window);
    os->shell.term_print("Calculator is running.\n");
    return 0;
}
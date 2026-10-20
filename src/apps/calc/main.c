#include "../../kernel/types.h"
#include "../../kernel/os_api.h"
#include "../../kernel/window.h"
#include "../../system/user/sys_api.h"

#define COLOR_WIN_BG      0x00C0C0C0
#define COLOR_DARK_GRAY   0x00808080
#define COLOR_LIGHT_GRAY  0x00DFDFDF
#define COLOR_WHITE       0x00FFFFFF
#define COLOR_BLACK       0x00000000
#define COLOR_BLUE        0x000000AA

#define BTN_W 36
#define BTN_H 26
#define BTN_GAP 6

static os_api_t user_api;
static os_api_t* os = &user_api;
static Window calc_window;
static uint32_t calc_window_handle;
static uint8_t calc_close_requested;

static int user_register_window(Window* window) {
    user_window_desc_t description = {0};
    description.x = window->x;
    description.y = window->y;
    description.width = window->w;
    description.height = window->h;
    uint32_t i = 0;
    while (i < USER_WINDOW_TITLE_MAX - 1u && window->title[i]) {
        description.title[i] = window->title[i];
        i++;
    }
    description.title[i] = '\0';
    int32_t handle = sys_window_create(&description);
    if (handle < 0) return -1;
    calc_window_handle = (uint32_t)handle;
    return 0;
}

static void user_unregister_window(Window* window) {
    (void)window;
    if (calc_window_handle) {
        sys_window_close(calc_window_handle);
        calc_window_handle = 0;
    }
}

static int user_draw(uint32_t operation, int32_t x, int32_t y, int32_t width,
                     int32_t height, const char* text, uint32_t color,
                     uint8_t pressed) {
    user_window_draw_t drawing = {0};
    drawing.operation = operation;
    drawing.x = x - calc_window.x;
    drawing.y = y - calc_window.y;
    drawing.width = width;
    drawing.height = height;
    drawing.text = text;
    drawing.color = color;
    drawing.pressed = pressed;
    return sys_window_draw(calc_window_handle, &drawing);
}

static void user_fill_rect(int32_t x, int32_t y, int32_t width, int32_t height,
                           uint32_t color) {
    (void)user_draw(USER_WINDOW_DRAW_FILL, x, y, width, height, 0, color, 0);
}

static void user_draw_string(int32_t x, int32_t y, const char* text,
                             uint32_t color) {
    (void)user_draw(USER_WINDOW_DRAW_TEXT, x, y, 0, 0, text, color, 0);
}

static void user_draw_button(int32_t x, int32_t y, int32_t width, int32_t height,
                             const char* label, uint8_t pressed) {
    (void)user_draw(USER_WINDOW_DRAW_BUTTON, x, y, width, height, label,
                    COLOR_WIN_BG, pressed);
}

static void user_blit_all(void) {
    if (calc_window_handle) (void)sys_window_present(calc_window_handle);
}

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
        (void)win;
        calc_close_requested = 1;
        return;
    }

    if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '*' || c == '/' || c == '=' || c == '\n' || c == 'c' || c == 'C') {
        handle_input(c);
        calc_draw_content(win);
        os->graphics.blit_all();
    }
}

void user_main(const char* arguments) {
    (void)arguments;
    calc_window_handle = 0;
    calc_close_requested = 0;
    user_api.graphics.fill_rect = user_fill_rect;
    user_api.graphics.draw_string = user_draw_string;
    user_api.graphics.draw_button = user_draw_button;
    user_api.graphics.blit_all = user_blit_all;
    user_api.windows.register_window = user_register_window;
    user_api.windows.unregister_window = user_unregister_window;

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

    if (os->windows.register_window(&calc_window) < 0) {
        static const char error[] = "calc: unable to create window\n";
        sys_write(error, sizeof(error) - 1u);
        return;
    }

    calc_draw_content(&calc_window);
    os->graphics.blit_all();
    while (!calc_close_requested) {
        user_window_event_t event;
        sys_yield();
        while (sys_window_poll(calc_window_handle, &event) == 1) {
            if (event.type == USER_WINDOW_EVENT_CLOSE) {
                calc_close_requested = 1;
                break;
            }
            if (event.type == USER_WINDOW_EVENT_REDRAW) {
                calc_window.x = event.x;
                calc_window.y = event.y;
                calc_draw_content(&calc_window);
            } else if (event.type == USER_WINDOW_EVENT_KEY) {
                calc_handle_key(&calc_window, (char)event.key);
            } else if (event.type == USER_WINDOW_EVENT_MOUSE) {
                calc_handle_mouse(&calc_window, event.x, event.y,
                                  (uint8_t)event.buttons);
            }
            if (calc_close_requested) break;
            os->graphics.blit_all();
        }
    }
    os->windows.unregister_window(&calc_window);
}

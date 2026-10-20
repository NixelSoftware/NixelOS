#include "terminal.h"
#include "../../shell/commands.h"

#define TERM_MAX_LINES 16
#define TERM_MAX_COLS 44

Window terminal_window;

static os_api_t* terminal_api;
static char term_lines[TERM_MAX_LINES][TERM_MAX_COLS + 1];
static int term_cursor_col;
static int term_cursor_row;

static void terminal_scroll(void) {
    for (int row = 0; row < TERM_MAX_LINES - 1; row++) {
        for (int col = 0; col <= TERM_MAX_COLS; col++) {
            term_lines[row][col] = term_lines[row + 1][col];
        }
    }
    for (int col = 0; col <= TERM_MAX_COLS; col++) {
        term_lines[TERM_MAX_LINES - 1][col] = 0;
    }
}

static void terminal_clear_output(void) {
    for (int row = 0; row < TERM_MAX_LINES; row++) {
        for (int col = 0; col <= TERM_MAX_COLS; col++) {
            term_lines[row][col] = 0;
        }
    }
    term_cursor_row = 0;
    term_cursor_col = 0;
}

static void terminal_append_output(const char* text) {
    while (*text) {
        if (*text == '\n') {
            if (term_cursor_row < TERM_MAX_LINES - 1) {
                term_cursor_row++;
            } else {
                terminal_scroll();
            }
            term_cursor_col = 0;
            term_lines[term_cursor_row][0] = '\0';
        } else if (term_cursor_col < TERM_MAX_COLS) {
            term_lines[term_cursor_row][term_cursor_col++] = *text;
            term_lines[term_cursor_row][term_cursor_col] = '\0';
        } else {
            if (term_cursor_row < TERM_MAX_LINES - 1) {
                term_cursor_row++;
            } else {
                terminal_scroll();
            }
            term_cursor_col = 0;
            term_lines[term_cursor_row][term_cursor_col++] = *text;
            term_lines[term_cursor_row][term_cursor_col] = '\0';
        }
        text++;
    }
}

static void terminal_draw(Window* win) {
    int32_t box_x = win->x + 6;
    int32_t box_y = win->y + 20;
    int32_t box_w = win->w - 12;
    int32_t box_h = win->h - 26;

    terminal_api->graphics.fill_rect(box_x, box_y, box_w, box_h, COLOR_BLACK);
    terminal_api->graphics.fill_rect(box_x - 1, box_y - 1, box_w + 2, 1, COLOR_DARK_GRAY);
    terminal_api->graphics.fill_rect(box_x - 1, box_y - 1, 1, box_h + 2, COLOR_DARK_GRAY);
    terminal_api->graphics.fill_rect(box_x - 1, box_y + box_h, box_w + 2, 1, COLOR_WHITE);
    terminal_api->graphics.fill_rect(box_x + box_w, box_y - 1, 1, box_h + 2, COLOR_WHITE);

    for (int row = 0; row < TERM_MAX_LINES; row++) {
        if (term_lines[row][0] != 0) {
            terminal_api->graphics.draw_string(box_x + 6, box_y + 6 + row * 12,
                                      term_lines[row], COLOR_WHITE);
        }
    }

    if (win->focused) {
        int caret_x = box_x + 6 + term_cursor_col * 8;
        int caret_y = box_y + 6 + term_cursor_row * 12;
        terminal_api->graphics.draw_string(caret_x, caret_y, "_", COLOR_WHITE);
    }
}

static void terminal_prompt(void) {
    if (term_cursor_col > 0) {
        if (term_cursor_row < TERM_MAX_LINES - 1) {
            term_cursor_row++;
        } else {
            terminal_scroll();
        }
    }
    term_lines[term_cursor_row][0] = '>';
    term_lines[term_cursor_row][1] = ' ';
    term_lines[term_cursor_row][2] = '\0';
    term_cursor_col = 2;
}

static void terminal_handle_key(Window* win, char c) {
    (void)win;

    if (c == '\b') {
        if (term_cursor_col > 2) {
            term_cursor_col--;
            term_lines[term_cursor_row][term_cursor_col] = 0;
        }
    } else if (c == '\n') {
        char command[TERM_MAX_COLS + 1];
        for (int i = 0; i <= TERM_MAX_COLS; i++) {
            command[i] = term_lines[term_cursor_row][i];
        }
        term_cursor_col = 0;
        term_cursor_row++;
        if (term_cursor_row >= TERM_MAX_LINES) {
            terminal_scroll();
            term_cursor_row = TERM_MAX_LINES - 1;
        }
        term_lines[term_cursor_row][0] = '\0';
        shell_execute_command(command, terminal_append_output, terminal_clear_output);
        terminal_prompt();
    } else if (c >= ' ' && c <= '~' && term_cursor_col < TERM_MAX_COLS) {
        term_lines[term_cursor_row][term_cursor_col++] = c;
        term_lines[term_cursor_row][term_cursor_col] = '\0';
    }
}

void terminal_init(os_api_t* api) {
    terminal_api = api;
    terminal_window.x = 100;
    terminal_window.y = 50;
    terminal_window.w = 380;
    terminal_window.h = 240;
    terminal_window.title = "Terminal";
    terminal_window.visible = 1;
    terminal_window.focused = 1;
    terminal_window.draw_content = terminal_draw;
    terminal_window.handle_key = terminal_handle_key;
    terminal_window.handle_mouse = 0;

    terminal_clear_output();
    terminal_prompt();
}

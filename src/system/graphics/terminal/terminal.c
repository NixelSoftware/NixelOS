#include "terminal.h"
#include "../../shell/commands.h"
#include "../wm/wm.h"
#include "../../../kernel/paging.h"

#define TERM_MAX_LINES 128
#define TERM_MAX_COLS 256

Window terminal_window;

static os_api_t* terminal_api;
static char term_lines[TERM_MAX_LINES][TERM_MAX_COLS + 1];
static int term_cursor_col;
static int term_cursor_row;
static uint32_t term_fg[TERM_MAX_LINES][TERM_MAX_COLS];
static uint32_t term_bg[TERM_MAX_LINES][TERM_MAX_COLS];
static const uint32_t ansi_palette[16] = {
    0x000000, 0xAA0000, 0x00AA00, 0xAA5500,
    0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA,
    0x555555, 0xFF5555, 0x55FF55, 0xFFFF55,
    0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF
};
static unsigned ansi_fg = 7, ansi_bg, ansi_bold, ansi_inverse;
static unsigned ansi_state, ansi_count, ansi_params[16], ansi_invalid;

static void terminal_reset_style(void) {
    ansi_fg = 7; ansi_bg = ansi_bold = ansi_inverse = 0;
}

static void terminal_sgr(void) {
    for (unsigned i = 0; i < ansi_count; ++i) {
        unsigned p = ansi_params[i];
        if (p == 0) terminal_reset_style();
        else if (p == 1) ansi_bold = 1;
        else if (p == 22) ansi_bold = 0;
        else if (p == 7) ansi_inverse = 1;
        else if (p == 27) ansi_inverse = 0;
        else if (p >= 30 && p <= 37) ansi_fg = p - 30;
        else if (p >= 40 && p <= 47) ansi_bg = p - 40;
        else if (p >= 90 && p <= 97) ansi_fg = p - 90 + 8;
        else if (p >= 100 && p <= 107) ansi_bg = p - 100 + 8;
        else if (p == 39) ansi_fg = 7;
        else if (p == 49) ansi_bg = 0;
        else if (p == 38 || p == 48) {
            if (i + 1 < ansi_count && ansi_params[i + 1] == 5) i += 2;
            else if (i + 1 < ansi_count && ansi_params[i + 1] == 2) i += 4;
            else break;
        }
    }
}

static void terminal_style_cell(int row, int col) {
    uint32_t fg = ansi_palette[ansi_fg < 8 && ansi_bold ? ansi_fg + 8 : ansi_fg];
    uint32_t bg = ansi_palette[ansi_bg];
    term_fg[row][col] = ansi_inverse ? bg : fg;
    term_bg[row][col] = ansi_inverse ? fg : bg;
}

static void terminal_draw(Window* win);

static void terminal_scroll(void) {
    for (int row = 0; row < TERM_MAX_LINES - 1; row++) {
        for (int col = 0; col <= TERM_MAX_COLS; col++) {
            term_lines[row][col] = term_lines[row + 1][col];
            if (col < TERM_MAX_COLS) {
                term_fg[row][col] = term_fg[row + 1][col];
                term_bg[row][col] = term_bg[row + 1][col];
            }
        }
    }
    for (int col = 0; col <= TERM_MAX_COLS; col++) {
        term_lines[TERM_MAX_LINES - 1][col] = 0;
    }
}

static void terminal_clear_output(void) {
    terminal_reset_style();
    ansi_state = ansi_invalid = 0;
    for (int row = 0; row < TERM_MAX_LINES; row++) {
        for (int col = 0; col <= TERM_MAX_COLS; col++) {
            term_lines[row][col] = 0;
            if (col < TERM_MAX_COLS) terminal_style_cell(row, col);
        }
    }
    term_cursor_row = 0;
    term_cursor_col = 0;
}

static void terminal_append_output(const char* text) {
    while (*text) {
        unsigned char c = (unsigned char)*text;
        if (c == 27) {
            ansi_state = 1;
            ++text;
            continue;
        }
        if (ansi_state) {
            ++text;
            if (ansi_state == 1) {
                ansi_state = c == '[' ? 2 : c == ']' ? 3 : 0;
                ansi_count = 1;
                ansi_params[0] = 0;
                ansi_invalid = 0;
            } else if (ansi_state == 3) {
                if (c == 7 || c == '\\') ansi_state = 0;
            } else if (c >= 0x40 && c <= 0x7E) {
                if (c == 'm' && !ansi_invalid) terminal_sgr();
                ansi_state = 0;
            } else if (c >= '0' && c <= '9') {
                unsigned* p = &ansi_params[ansi_count - 1];
                if (*p > 999) ansi_invalid = 1;
                else *p = *p * 10 + c - '0';
            } else if (c == ';') {
                if (ansi_count == 16) ansi_invalid = 1;
                else ansi_params[ansi_count++] = 0;
            } else ansi_invalid = 1;
            continue;
        }
        if (*text == '\r') {
            text++;
            continue;
        } else if (*text == '\n') {
            if (term_cursor_row < TERM_MAX_LINES - 1) {
                term_cursor_row++;
            } else {
                terminal_scroll();
            }
            term_cursor_col = 0;
            term_lines[term_cursor_row][0] = '\0';
        } else if (term_cursor_col < TERM_MAX_COLS) {
            if (c < 32 || c == 127) { ++text; continue; }
            terminal_style_cell(term_cursor_row, term_cursor_col);
            term_lines[term_cursor_row][term_cursor_col++] = *text;
            term_lines[term_cursor_row][term_cursor_col] = '\0';
        } else {
            if (term_cursor_row < TERM_MAX_LINES - 1) {
                term_cursor_row++;
            } else {
                terminal_scroll();
            }
            term_cursor_col = 0;
            if (c < 32 || c == 127) { ++text; continue; }
            terminal_style_cell(term_cursor_row, term_cursor_col);
            term_lines[term_cursor_row][term_cursor_col++] = *text;
            term_lines[term_cursor_row][term_cursor_col] = '\0';
        }
        text++;
    }
}

void terminal_write(const char* text, uint32_t length) {
    char chunk[257];
    while (length) {
        uint32_t count = length < sizeof(chunk) - 1u
            ? length : sizeof(chunk) - 1u;
        for (uint32_t i = 0; i < count; i++) chunk[i] = text[i];
        chunk[count] = '\0';
        terminal_append_output(chunk);
        text += count;
        length -= count;
    }
    if (terminal_api) {
        uint32_t previous_directory = paging_current_directory();
        paging_activate_kernel();
        terminal_window.visible = 1;
        wm_bring_to_front(&terminal_window);
        paging_activate_directory(previous_directory);
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

    int cols = (win->w - 24) / 8;
    int rows = (win->h - 38) / 12;
    if (cols < 1 || rows < 1) return;
    int total = 0;
    for (int r = 0; r <= term_cursor_row; ++r) {
        int len = 0;
        while (term_lines[r][len]) ++len;
        int used = r == term_cursor_row ? term_cursor_col + 1 : len;
        total += used ? (used + cols - 1) / cols : 1;
    }
    int first = total > rows ? total - rows : 0;
    int visual = 0;
    for (int r = 0; r <= term_cursor_row; ++r) {
        int len = 0;
        while (term_lines[r][len]) ++len;
        for (int c = 0; c < len; ++c) {
            int line = visual + c / cols - first;
            if (line < 0 || line >= rows) continue;
            int x = box_x + 6 + (c % cols) * 8;
            int y = box_y + 6 + line * 12;
            char glyph[2] = {term_lines[r][c], 0};
            terminal_api->graphics.fill_rect(x, y, 8, 12, term_bg[r][c]);
            terminal_api->graphics.draw_string(x, y, glyph, term_fg[r][c]);
        }
        if (r == term_cursor_row && win->focused) {
            int line = visual + term_cursor_col / cols - first;
            if (line >= 0 && line < rows)
                terminal_api->graphics.draw_string(
                    box_x + 6 + term_cursor_col % cols * 8,
                    box_y + 6 + line * 12, "_", COLOR_WHITE);
        }
        int used = r == term_cursor_row ? term_cursor_col + 1 : len;
        visual += used ? (used + cols - 1) / cols : 1;
    }
    /* Bottom-right resize grip. */
    for (int i = 0; i < 3; ++i)
        terminal_api->graphics.fill_rect(win->x + win->w - 5 - i * 3,
                                        win->y + win->h - 4, 2, 2, COLOR_DARK_GRAY);
}

static void terminal_prompt(void) {
    terminal_reset_style();
    ansi_state = 0;
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
    terminal_style_cell(term_cursor_row, 0);
    terminal_style_cell(term_cursor_row, 1);
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
        terminal_style_cell(term_cursor_row, term_cursor_col);
        term_lines[term_cursor_row][term_cursor_col++] = c;
        term_lines[term_cursor_row][term_cursor_col] = '\0';
    }
}

void terminal_init(os_api_t* api) {
    terminal_api = api;
    terminal_window.x = 10;
    terminal_window.y = 30;
    terminal_window.w = 300;
    terminal_window.h = 200;
    terminal_window.title = "Terminal";
    terminal_window.visible = 1;
    terminal_window.focused = 1;
    terminal_window.draw_content = terminal_draw;
    terminal_window.handle_key = terminal_handle_key;
    terminal_window.handle_mouse = 0;

    terminal_clear_output();
    terminal_prompt();
}

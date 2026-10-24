#include "../../../kernel/mutex.h"
#include "terminal.h"
#include "../../shell/commands.h"
#include "../wm/wm.h"
#include "../../../kernel/paging.h"

#define TERM_MAX_LINES 128
#define TERM_MAX_COLS 256

static os_api_t* terminal_api;
struct terminal {
    Window window;
    uint8_t active;
    char title[24];
    char term_lines[TERM_MAX_LINES][TERM_MAX_COLS + 1];
    int term_cursor_col, term_cursor_row;
    char term_input[TERM_MAX_COLS - 1];
    int term_input_length, term_editing;
    int term_output_row, term_output_col;
    uint8_t term_fg[TERM_MAX_LINES][TERM_MAX_COLS];
    uint8_t term_bg[TERM_MAX_LINES][TERM_MAX_COLS];
    char password[64];
    unsigned password_length;
    int password_pending, password_done;
    unsigned ansi_fg, ansi_bg, ansi_bold, ansi_inverse;
    unsigned ansi_state, ansi_count, ansi_params[16], ansi_invalid;
};
static terminal_t terminals[TERMINAL_MAX_INSTANCES];

static const uint32_t ansi_palette[16] = {
    0x000000, 0xAA0000, 0x00AA00, 0xAA5500,
    0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA,
    0x555555, 0xFF5555, 0x55FF55, 0xFFFF55,
    0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF
};

static void terminal_reset_style(terminal_t* t) {
    t->ansi_fg = 7; t->ansi_bg = t->ansi_bold = t->ansi_inverse = 0;
}

static void terminal_sgr(terminal_t* t) {
    for (unsigned i = 0; i < t->ansi_count; ++i) {
        unsigned p = t->ansi_params[i];
        if (p == 0) terminal_reset_style(t);
        else if (p == 1) t->ansi_bold = 1;
        else if (p == 22) t->ansi_bold = 0;
        else if (p == 7) t->ansi_inverse = 1;
        else if (p == 27) t->ansi_inverse = 0;
        else if (p >= 30 && p <= 37) t->ansi_fg = p - 30;
        else if (p >= 40 && p <= 47) t->ansi_bg = p - 40;
        else if (p >= 90 && p <= 97) t->ansi_fg = p - 90 + 8;
        else if (p >= 100 && p <= 107) t->ansi_bg = p - 100 + 8;
        else if (p == 39) t->ansi_fg = 7;
        else if (p == 49) t->ansi_bg = 0;
        else if (p == 38 || p == 48) {
            if (i + 1 < t->ansi_count && t->ansi_params[i + 1] == 5) i += 2;
            else if (i + 1 < t->ansi_count && t->ansi_params[i + 1] == 2) i += 4;
            else break;
        }
    }
}

static void terminal_style_cell(terminal_t* t, int row, int col) {
    uint8_t fg = t->ansi_fg < 8 && t->ansi_bold ? t->ansi_fg + 8 : t->ansi_fg;
    uint8_t bg = t->ansi_bg;
    t->term_fg[row][col] = t->ansi_inverse ? bg : fg;
    t->term_bg[row][col] = t->ansi_inverse ? fg : bg;
}

static void terminal_draw(Window* win);

static void terminal_scroll(terminal_t* t) {
    for (int row = 0; row < TERM_MAX_LINES - 1; row++) {
        for (int col = 0; col <= TERM_MAX_COLS; col++) {
            t->term_lines[row][col] = t->term_lines[row + 1][col];
            if (col < TERM_MAX_COLS) {
                t->term_fg[row][col] = t->term_fg[row + 1][col];
                t->term_bg[row][col] = t->term_bg[row + 1][col];
            }
        }
    }
    for (int col = 0; col <= TERM_MAX_COLS; col++) {
        t->term_lines[TERM_MAX_LINES - 1][col] = 0;
    }
}

static void terminal_clear_output(terminal_t* t) {
    terminal_reset_style(t);
    t->term_editing = t->term_input_length = 0;
    t->ansi_state = t->ansi_invalid = 0;
    for (int row = 0; row < TERM_MAX_LINES; row++) {
        for (int col = 0; col <= TERM_MAX_COLS; col++) {
            t->term_lines[row][col] = 0;
            if (col < TERM_MAX_COLS) terminal_style_cell(t, row, col);
        }
    }
    t->term_cursor_row = 0;
    t->term_cursor_col = 0;
}

static void terminal_append_output(terminal_t* t, const char* text) {
    while (*text) {
        unsigned char c = (unsigned char)*text;
        if (c == 27) {
            t->ansi_state = 1;
            ++text;
            continue;
        }
        if (t->ansi_state) {
            ++text;
            if (t->ansi_state == 1) {
                t->ansi_state = c == '[' ? 2 : c == ']' ? 3 : 0;
                t->ansi_count = 1;
                t->ansi_params[0] = 0;
                t->ansi_invalid = 0;
            } else if (t->ansi_state == 3) {
                if (c == 7 || c == '\\') t->ansi_state = 0;
            } else if (c >= 0x40 && c <= 0x7E) {
                if (c == 'm' && !t->ansi_invalid) terminal_sgr(t);
                t->ansi_state = 0;
            } else if (c >= '0' && c <= '9') {
                unsigned* p = &t->ansi_params[t->ansi_count - 1];
                if (*p > 999) t->ansi_invalid = 1;
                else *p = *p * 10 + c - '0';
            } else if (c == ';') {
                if (t->ansi_count == 16) t->ansi_invalid = 1;
                else t->ansi_params[t->ansi_count++] = 0;
            } else t->ansi_invalid = 1;
            continue;
        }
        if (*text == '\r') {
            text++;
            continue;
        } else if (*text == '\n') {
            if (t->term_cursor_row < TERM_MAX_LINES - 1) {
                t->term_cursor_row++;
            } else {
                terminal_scroll(t);
            }
            t->term_cursor_col = 0;
            t->term_lines[t->term_cursor_row][0] = '\0';
        } else if (t->term_cursor_col < TERM_MAX_COLS) {
            if (c < 32 || c == 127) { ++text; continue; }
            terminal_style_cell(t, t->term_cursor_row, t->term_cursor_col);
            t->term_lines[t->term_cursor_row][t->term_cursor_col++] = *text;
            t->term_lines[t->term_cursor_row][t->term_cursor_col] = '\0';
        } else {
            if (t->term_cursor_row < TERM_MAX_LINES - 1) {
                t->term_cursor_row++;
            } else {
                terminal_scroll(t);
            }
            t->term_cursor_col = 0;
            if (c < 32 || c == 127) { ++text; continue; }
            terminal_style_cell(t, t->term_cursor_row, t->term_cursor_col);
            t->term_lines[t->term_cursor_row][t->term_cursor_col++] = *text;
            t->term_lines[t->term_cursor_row][t->term_cursor_col] = '\0';
        }
        text++;
    }
}

/* Keep background output separate from the command being edited. Preserve
 * the output cursor and ANSI parser across short SYS_WRITE calls. */
static void terminal_show_input(terminal_t* t) {
    t->term_output_row = t->term_cursor_row;
    t->term_output_col = t->term_cursor_col;
    if (t->term_cursor_col) {
        if (t->term_cursor_row < TERM_MAX_LINES - 1) t->term_cursor_row++;
        else { terminal_scroll(t); t->term_output_row--; }
    }
    t->term_lines[t->term_cursor_row][0] = '>';
    t->term_lines[t->term_cursor_row][1] = ' ';
    for (int i = 0; i < t->term_input_length; i++)
        t->term_lines[t->term_cursor_row][i + 2] = t->term_input[i];
    t->term_cursor_col = t->term_input_length + 2;
    t->term_lines[t->term_cursor_row][t->term_cursor_col] = 0;
    for (int i = 0; i < t->term_cursor_col; i++) {
        t->term_fg[t->term_cursor_row][i] = 7;
        t->term_bg[t->term_cursor_row][i] = 0;
    }
}

void terminal_write(const char* text, uint32_t length) {
    MUTEX_GUARD(kernel_mutex);
    terminal_t* t = current_task ? current_task->terminal : 0;
    if (!t || !t->active || !length) return;
    if (t->term_editing) {
        t->term_lines[t->term_cursor_row][0] = 0;
        t->term_cursor_row = t->term_output_row;
        t->term_cursor_col = t->term_output_col;
    }
    char chunk[257];
    while (length) {
        uint32_t count = length < sizeof(chunk) - 1u ? length : sizeof(chunk) - 1u;
        for (uint32_t i = 0; i < count; i++) chunk[i] = text[i];
        chunk[count] = 0;
        terminal_append_output(t, chunk);
        text += count;
        length -= count;
    }
    if (t->term_editing) terminal_show_input(t);
    /* Background output must not steal focus from the window being used. */
}

static void terminal_draw(Window* win) {
    terminal_t* t = terminal_for_window(win);
    if (!t) return;
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
    for (int r = 0; r <= t->term_cursor_row; ++r) {
        int len = 0;
        while (t->term_lines[r][len]) ++len;
        int used = r == t->term_cursor_row ? t->term_cursor_col + 1 : len;
        total += used ? (used + cols - 1) / cols : 1;
    }
    int first = total > rows ? total - rows : 0;
    int visual = 0;
    for (int r = 0; r <= t->term_cursor_row; ++r) {
        int len = 0;
        while (t->term_lines[r][len]) ++len;
        for (int c = 0; c < len; ++c) {
            int line = visual + c / cols - first;
            if (line < 0 || line >= rows) continue;
            int x = box_x + 6 + (c % cols) * 8;
            int y = box_y + 6 + line * 12;
            char glyph[2] = {t->term_lines[r][c], 0};
            terminal_api->graphics.fill_rect(x, y, 8, 12, ansi_palette[t->term_bg[r][c]]);
            terminal_api->graphics.draw_string(x, y, glyph, ansi_palette[t->term_fg[r][c]]);
        }
        if (r == t->term_cursor_row && win->focused) {
            int line = visual + t->term_cursor_col / cols - first;
            if (line >= 0 && line < rows)
                terminal_api->graphics.draw_string(
                    box_x + 6 + t->term_cursor_col % cols * 8,
                    box_y + 6 + line * 12, "_", COLOR_WHITE);
        }
        int used = r == t->term_cursor_row ? t->term_cursor_col + 1 : len;
        visual += used ? (used + cols - 1) / cols : 1;
    }
    /* Bottom-right resize grip. */
    for (int i = 0; i < 3; ++i)
        terminal_api->graphics.fill_rect(win->x + win->w - 5 - i * 3,
                                        win->y + win->h - 4, 2, 2, COLOR_DARK_GRAY);
}

static void terminal_prompt(terminal_t* t) {
    terminal_reset_style(t);
    t->ansi_state = 0;
    if (t->term_cursor_col) {
        if (t->term_cursor_row < TERM_MAX_LINES - 1) t->term_cursor_row++;
        else terminal_scroll(t);
        t->term_cursor_col = 0;
    }
    t->term_input_length = 0;
    t->term_input[0] = 0;
    t->term_editing = 1;
    terminal_show_input(t);
}

static void terminal_shell_output(const char* text) {
    uint32_t length = 0;
    while (text[length]) length++;
    terminal_write(text, length);
    wm_invalidate();
}
static void terminal_shell_clear(void) {
    MUTEX_GUARD(kernel_mutex);
    terminal_t* t = current_task ? current_task->terminal : 0;
    if (!t || !t->active) return;
    char input[sizeof(t->term_input)];
    int editing = t->term_editing;
    int length = editing ? t->term_input_length : 0;
    for (int i = 0; i <= length; i++) input[i] = t->term_input[i];
    terminal_clear_output(t);
    t->term_input[0] = 0;
    /* Submitted commands are no longer editable input. The shell adds a
     * fresh prompt when the command finishes. */
    if (editing) {
        for (int i = 0; i <= length; i++) t->term_input[i] = input[i];
        t->term_input_length = length;
        t->term_editing = 1;
        terminal_show_input(t);
    }
    wm_invalidate();
}

static void terminal_handle_key(Window* win, char c) {
    terminal_t* t = terminal_for_window(win);
    if (!t) return;
    if (t->password_pending) {
        if (c == '\n' || (uint8_t)c == 3) {
            t->password_done = (uint8_t)c == 3 ? -1 : 1;
            t->password_pending = 0;
            terminal_append_output(t, (uint8_t)c == 3 ? "^C\n" : "\n");
        } else if (c == '\b') {
            if (t->password_length) t->password[--t->password_length] = 0;
        } else if (c >= ' ' && c <= '~' && t->password_length < sizeof(t->password) - 1) {
            t->password[t->password_length++] = c;
            t->password[t->password_length] = 0;
        }
        wm_invalidate();
        return;
    }
    if ((uint8_t)c == 3) {
        int was_editing = t->term_editing;
        t->term_editing = 0;
        terminal_reset_style(t);
        t->ansi_state = 0;
        /* Preserve the partial command on screen, then cancel its input. */
        terminal_append_output(t, "^C\n");
        t->term_input_length = 0;
        t->term_input[0] = 0;
        int running = shell_interrupt(t);
        if (was_editing || !running) terminal_prompt(t);
        wm_invalidate();
        return;
    }
    if (!t->term_editing) return;
    if (c == '\b') {
        if (t->term_input_length) {
            t->term_input[--t->term_input_length] = 0;
            t->term_lines[t->term_cursor_row][--t->term_cursor_col] = 0;
        }
    } else if (c == '\n') {
        char command[TERM_MAX_COLS + 1];
        for (int i = 0; i <= t->term_input_length; i++) command[i] = t->term_input[i];
        t->term_editing = 0;
        t->term_cursor_col = 0;
        if (t->term_cursor_row < TERM_MAX_LINES - 1) t->term_cursor_row++;
        else terminal_scroll(t);
        t->term_lines[t->term_cursor_row][0] = 0;
        if (!shell_submit_command(t, command, terminal_shell_output, terminal_shell_clear)) {
            terminal_append_output(t, "Command queue is full.\n");
            terminal_prompt(t);
        }
    } else if (c >= ' ' && c <= '~' && t->term_cursor_col < TERM_MAX_COLS) {
        t->term_input[t->term_input_length++] = c;
        t->term_input[t->term_input_length] = 0;
        t->term_fg[t->term_cursor_row][t->term_cursor_col] = 7;
        t->term_bg[t->term_cursor_row][t->term_cursor_col] = 0;
        t->term_lines[t->term_cursor_row][t->term_cursor_col++] = c;
        t->term_lines[t->term_cursor_row][t->term_cursor_col] = 0;
    }
}

void terminal_init(os_api_t* api) { terminal_api = api; }

terminal_t* terminal_for_window(Window* window) {
    for (int i = 0; i < TERMINAL_MAX_INSTANCES; i++)
        if (terminals[i].active && &terminals[i].window == window) return &terminals[i];
    return 0;
}
int terminal_launch(void) {
    MUTEX_GUARD(kernel_mutex);
    if (!terminal_api) return -1;
    terminal_t* t = 0;
    int slot;
    for (slot = 0; slot < TERMINAL_MAX_INSTANCES; slot++)
        if (!terminals[slot].active) { t = &terminals[slot]; break; }
    if (!t) return -1;
    t->active = 1;
    const char* title = "Terminal";
    int n = 0;
    while (title[n]) { t->title[n] = title[n]; n++; }
    t->title[n] = 0;
    t->window = (Window){
        .x = 90 + slot * 32, .y = 40 + slot * 28, .w = 360, .h = 240,
        .title = t->title, .visible = 1, .focused = 1,
        .draw_content = terminal_draw, .handle_key = terminal_handle_key
    };
    terminal_clear_output(t);
    terminal_prompt(t);
    if (wm_register_window(&t->window) < 0) { t->active = 0; return -1; }
    if (shell_open_terminal(t) < 0) {
        wm_unregister_window(&t->window); t->active = 0; return -1;
    }
    wm_invalidate();
    return slot;
}
void terminal_close(Window* window) {
    MUTEX_GUARD(kernel_mutex);
    terminal_t* t = terminal_for_window(window);
    if (!t) return;
    wm_unregister_window(window);
    window->visible = 0;
    shell_close_terminal(t);
}
void terminal_release(terminal_t* t) {
    MUTEX_GUARD(kernel_mutex);
    wm_unregister_window(&t->window);
    t->window.visible = 0;
    t->active = 0;
}
void terminal_command_done(terminal_t* t) {
    MUTEX_GUARD(kernel_mutex);
    terminal_prompt(t);
    wm_invalidate();
}

/* The shell sleeps while the window task collects input without echo. */
int terminal_read_password(char* output, uint32_t capacity) {
    mutex_lock(&kernel_mutex);
    terminal_t* t = current_task ? current_task->terminal : 0;
    if (!t || !t->active || !output || capacity < sizeof(t->password)) {
        mutex_unlock(&kernel_mutex);
        return 0;
    }
    t->password_length = 0;
    t->password[0] = 0;
    t->password_done = 0;
    t->password_pending = 1;
    while (!t->password_done && t->active && !current_task->terminate_requested) {
        mutex_unlock(&kernel_mutex);
        task_sleep_ms(1);
        mutex_lock(&kernel_mutex);
    }
    int ok = t->password_done == 1 && t->active && !current_task->terminate_requested;
    for (unsigned i = 0; i < sizeof(t->password); i++) {
        output[i] = ok ? t->password[i] : 0;
        ((volatile char*)t->password)[i] = 0;
    }
    t->password_pending = t->password_done = 0;
    t->password_length = 0;
    mutex_unlock(&kernel_mutex);
    return ok;
}

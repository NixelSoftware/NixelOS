#include "types.h"
#include "cursor.h"
#include "font.h"
#include "keyboard.h"
#include "window.h"
#include "desktop.h"
#include "drivers/display.h"
#include "idt.h"
#include "fs.h" 
#include "os_api.h"
#include "task.h"
#include "drivers/net.h"
#include "net/arp.h"
#include "net/icmp.h"
#include "net/ipv4.h"
#include "dialog.h"
#include "../apps/notepad/notepad.h"
#include "../apps/viewer/viewer.h"

#define TASKBAR_Y       568
#define TASKBAR_HEIGHT  32

#define TERM_MAX_LINES 16
#define TERM_MAX_COLS  44

#define MOUSE_CURSOR_W 12
#define MOUSE_CURSOR_H 19

#define NUM_WINDOWS 6

static void term_print(const char* text);
static int str_prefix(const char* str, const char* prefix);
static void update_cursor_fast(int32_t new_x, int32_t new_y);
static void cursor_save(int32_t x, int32_t y);
extern void irq0_timer_handler(void);

volatile uint32_t frame_timer = 0;
static int32_t mouse_x = 400;
static int32_t mouse_y = 300;
static int32_t old_mouse_x = 400;
static int32_t old_mouse_y = 300;
static uint32_t cursor_saved_bg[MOUSE_CURSOR_W * MOUSE_CURSOR_H];
static uint8_t cursor_bg_valid = 0;

static Window* dragging_window = 0;
static uint8_t prev_left_click = 0;
static uint8_t start_menu_open = 0;
static uint8_t needs_redraw = 1;

static Window terminal_window;
static char term_lines[TERM_MAX_LINES][TERM_MAX_COLS + 1];
static int term_cursor_col = 0;
static int term_cursor_row = 0;

static Window* windows[NUM_WINDOWS] = {0};

static volatile uint8_t ping_reply_received;
static volatile uint32_t ping_reply_time;
static uint32_t ping_target_ip;
static uint32_t ping_start_time;
static uint16_t ping_sequence;

static void print_u32(uint32_t value) {
    char digits[11];
    int count = 0;
    if (value == 0) {
        term_print("0");
        return;
    }
    while (value != 0) {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (count > 0) {
        char digit[2] = { digits[--count], '\0' };
        term_print(digit);
    }
}

static int parse_ipv4(const char* text, uint32_t* address) {
    uint32_t result = 0;
    int parts = 0;

    while (*text != '\0' && parts < 4) {
        uint32_t value = 0;
        int digits = 0;
        while (*text >= '0' && *text <= '9') {
            value = value * 10 + (uint32_t)(*text - '0');
            if (value > 255) return 0;
            text++;
            digits++;
        }
        if (digits == 0) return 0;
        result = (result << 8) | value;
        parts++;
        if (parts < 4) {
            if (*text != '.') return 0;
            text++;
        }
    }
    if (parts != 4 || *text != '\0') return 0;
    *address = result;
    return 1;
}

static void ping_reply(uint32_t source_ip, uint16_t identifier,
                       uint16_t sequence, const uint8_t* payload,
                       uint16_t length) {
    (void)identifier;
    (void)payload;
    if (source_ip == ping_target_ip && sequence == ping_sequence) {
        ping_reply_time = frame_timer - ping_start_time;
        ping_reply_received = 1;
        (void)length;
    }
}

static void terminal_ping(const char* argument) {
    uint32_t address;
    uint32_t received = 0;
    uint32_t total = 0;
    uint32_t minimum = 0xFFFFFFFF;
    uint32_t maximum = 0;
    char host[32];
    int host_length = 0;
    const uint8_t payload[] = "operating-system";

    if (str_prefix(argument, "http://")) argument += 7;
    else if (str_prefix(argument, "https://")) argument += 8;
    while (argument[host_length] != '\0'
           && argument[host_length] != '/'
           && argument[host_length] != ' '
           && host_length < (int)sizeof(host) - 1) {
        host[host_length] = argument[host_length];
        host_length++;
    }
    host[host_length] = '\0';

    if (!parse_ipv4(host, &address)) {
        term_print("Usage: ping <IPv4 address>\n");
        term_print("URL paths are accepted for IPv4 URLs; DNS is not supported.\n");
        return;
    }

    term_print("PING ");
    term_print(host);
    term_print(" (4 packets)\n");

    ping_target_ip = address;
    for (ping_sequence = 1; ping_sequence <= 4; ping_sequence++) {
        uint32_t deadline = frame_timer + 200;
        uint32_t wait_ticks = 0;
        uint8_t sent;
        ping_reply_received = 0;
        ping_start_time = frame_timer;

        sent = icmp_request_echo(address, 0x4F53, ping_sequence,
                                 payload, sizeof(payload) - 1);
        while (!sent && frame_timer < deadline && wait_ticks++ < 200) {
            net_poll();
            sent = icmp_request_echo(address, 0x4F53, ping_sequence,
                                     payload, sizeof(payload) - 1);
            if (!sent) __asm__ volatile ("pause");
        }

        wait_ticks = 0;
        while (sent && !ping_reply_received && frame_timer < deadline
               && wait_ticks++ < 200) {
            net_poll();
            __asm__ volatile ("pause");
        }

        if (ping_reply_received) {
            uint32_t milliseconds = ping_reply_time * 10;
            term_print("reply ");
            print_u32(ping_sequence);
            term_print(": ");
            print_u32(milliseconds);
            term_print(" ms\n");
            received++;
            total += milliseconds;
            if (milliseconds < minimum) minimum = milliseconds;
            if (milliseconds > maximum) maximum = milliseconds;
        } else {
            term_print("timeout ");
            print_u32(ping_sequence);
            term_print("\n");
        }
    }

    term_print("--- ping statistics ---\n");
    print_u32(4 - received);
    term_print(" packets lost (");
    print_u32((4 - received) * 25);
    term_print("%)\n");
    if (received != 0) {
        term_print("min/avg/max = ");
        print_u32(minimum);
        term_print("/");
        print_u32(total / received);
        term_print("/");
        print_u32(maximum);
        term_print(" ms\n");
    }
}


static uint8_t mouse_cycle = 0;
static uint8_t mouse_packet[3];

static void pit_init(uint32_t frequency_hz) {
    uint32_t divisor = 1193182 / frequency_hz;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    display_fill_rect(x, y, w, h, color);
}

void draw_button(int32_t x, int32_t y, int32_t w, int32_t h, const char* label, uint8_t pressed) {
    fill_rect(x, y, w, h, COLOR_WIN_BG);

    uint32_t top_left = pressed ? COLOR_DARK_GRAY : COLOR_WHITE;
    uint32_t bottom_right = pressed ? COLOR_WHITE : COLOR_DARK_GRAY;

    fill_rect(x, y, w, 1, top_left);
    fill_rect(x, y, 1, h, top_left);
    fill_rect(x, y + h - 1, w, 1, bottom_right);
    fill_rect(x + w - 1, y, 1, h, bottom_right);

    if (label) {
        int offset = pressed ? 1 : 0;
        draw_string(x + 6 + offset, y + (h / 2) - 4 + offset, label, COLOR_BLACK);
    }
}

void draw_window_frame(const Window* win) {
    if (!win || !win->visible) return;

    fill_rect(win->x, win->y, win->w, win->h, COLOR_WIN_BG);

    uint32_t bar_color = win->focused ? COLOR_TITLE_BAR : COLOR_DARK_GRAY;
    fill_rect(win->x + 2, win->y + 2, win->w - 4, 15, bar_color);

    if (win->title) {
        draw_string(win->x + 6, win->y + 5, win->title, COLOR_WHITE);
    }

    int32_t btn_x = win->x + win->w - 16;
    int32_t btn_y = win->y + 3;
    draw_button(btn_x, btn_y, 13, 13, 0, 0);

    uint32_t* buf = display_get_backbuffer();
    for (int i = 0; i < 7; i++) {
        buf[(btn_y + 3 + i) * SCREEN_WIDTH + (btn_x + 3 + i)] = COLOR_BLACK;
        buf[(btn_y + 3 + i) * SCREEN_WIDTH + (btn_x + 9 - i)] = COLOR_BLACK;
    }

    fill_rect(win->x, win->y, win->w, 1, COLOR_WHITE);
    fill_rect(win->x, win->y, 1, win->h, COLOR_WHITE);
    fill_rect(win->x, win->y + win->h - 1, win->w, 1, COLOR_DARK_GRAY);
    fill_rect(win->x + win->w - 1, win->y, 1, win->h, COLOR_DARK_GRAY);

    if (win->draw_content) {
        win->draw_content((Window*)win);
    }
}

void bring_to_front(Window* target) {
    if (!target) return;

    for (int i = 0; i < NUM_WINDOWS; i++) {
        if (windows[i]) {
            windows[i]->focused = (windows[i] == target);
        }
    }

    int target_idx = -1;
    for (int i = 0; i < NUM_WINDOWS; i++) {
        if (windows[i] == target) {
            target_idx = i;
            break;
        }
    }

    if (target_idx != -1) {
        
        int last_occupied = NUM_WINDOWS - 1;
        while (last_occupied > target_idx && windows[last_occupied] == 0) {
            last_occupied--;
        }

        for (int j = target_idx; j < last_occupied; j++) {
            windows[j] = windows[j + 1];
        }
        windows[last_occupied] = target;
    }
}

static int api_register_window(Window* win) {
    if (!win) return -1;

    
    for (int i = 0; i < NUM_WINDOWS; i++) {
        if (windows[i] == win) {
            win->visible = 1;
            win->focused = 1;
            bring_to_front(win);
            needs_redraw = 1;
            return i;
        }
    }

    
    for (int i = 0; i < NUM_WINDOWS; i++) {
        if (windows[i] == 0) {
            windows[i] = win;
            win->visible = 1;
            win->focused = 1;

            for (int j = 0; j < NUM_WINDOWS; j++) {
                if (windows[j] && windows[j] != win) {
                    windows[j]->focused = 0;
                }
            }
            bring_to_front(win);
            needs_redraw = 1;
            return i;
        }
    }
    return -1;
}

static void api_unregister_window(Window* win) {
    if (!win) return;
    for (int i = 0; i < NUM_WINDOWS; i++) {
        if (windows[i] == win) {
            windows[i] = 0;
            
            for (int j = i; j < NUM_WINDOWS - 1; j++) {
                windows[j] = windows[j + 1];
            }
            windows[NUM_WINDOWS - 1] = 0;
            needs_redraw = 1;
            break;
        }
    }
}

static uint8_t child_poll_events(int32_t* out_mx, int32_t* out_my, uint8_t* out_btn) {
    uint8_t scancode_out = 0;

    uint8_t status = inb(0x64);
    if (status & 0x01) {
        if (status & 0x20) {
            uint8_t b = inb(0x60);
            if (mouse_cycle == 0) {
                if (b & 0x08) {
                    mouse_packet[0] = b;
                    mouse_cycle = 1;
                }
            } else if (mouse_cycle == 1) {
                mouse_packet[1] = b;
                mouse_cycle = 2;
            } else if (mouse_cycle == 2) {
                mouse_packet[2] = b;
                mouse_cycle = 0;

                uint8_t flags = mouse_packet[0];
                int8_t dx = (int8_t)mouse_packet[1];
                int8_t dy = (int8_t)mouse_packet[2];

                if (!(flags & 0xC0)) {
                    if (dx != 0 || dy != 0) {
                        mouse_x += dx;
                        mouse_y -= dy;
                        if (mouse_x < 0) mouse_x = 0;
                        if (mouse_x > SCREEN_WIDTH - 12) mouse_x = SCREEN_WIDTH - 12;
                        if (mouse_y < 0) mouse_y = 0;
                        if (mouse_y > SCREEN_HEIGHT - 19) mouse_y = SCREEN_HEIGHT - 19;

                        update_cursor_fast(mouse_x, mouse_y);
                    }
                    prev_left_click = flags & 0x01;
                }
            }
        } else {
            uint8_t sc = inb(0x60);
            if (!(sc & 0x80)) {
                scancode_out = sc;
            }
        }
    }

    if (out_mx)  *out_mx = mouse_x;
    if (out_my)  *out_my = mouse_y;
    if (out_btn) *out_btn = prev_left_click;

    return scancode_out;
}

static void api_sync_cursor(void) {
    cursor_save(mouse_x, mouse_y);
    draw_cursor(mouse_x, mouse_y);
    display_blit_rect(mouse_x, mouse_y, MOUSE_CURSOR_W, MOUSE_CURSOR_H);
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;
}

static os_api_t kernel_api = {
    .fill_rect          = fill_rect,
    .draw_string        = draw_string,
    .draw_button        = draw_button,
    .blit_rect          = display_blit_rect,
    .blit_all           = display_blit,
    .fs_list            = fs_list,
    .fs_read            = fs_read_file,
    .fs_write           = fs_write_file,
    .term_print         = term_print,
    .register_window    = api_register_window,
    .unregister_window  = api_unregister_window,
    .show_info_dialog = show_info_dialog
};

static void term_scroll(void) {
    for (int r = 0; r < TERM_MAX_LINES - 1; r++) {
        for (int col = 0; col <= TERM_MAX_COLS; col++) {
            term_lines[r][col] = term_lines[r + 1][col];
        }
    }
    for (int col = 0; col <= TERM_MAX_COLS; col++) {
        term_lines[TERM_MAX_LINES - 1][col] = 0;
    }
}

static void term_print(const char* text) {
    while (*text) {
        if (*text == '\n') {
            if (term_cursor_row < TERM_MAX_LINES - 1) {
                term_cursor_row++;
            } else {
                term_scroll();
            }
            term_cursor_col = 0;
            term_lines[term_cursor_row][0] = '\0';
        } else {
            if (term_cursor_col < TERM_MAX_COLS) {
                term_lines[term_cursor_row][term_cursor_col++] = *text;
                term_lines[term_cursor_row][term_cursor_col] = '\0';
            } else {
                if (term_cursor_row < TERM_MAX_LINES - 1) {
                    term_cursor_row++;
                } else {
                    term_scroll();
                }
                term_cursor_col = 0;
                term_lines[term_cursor_row][term_cursor_col++] = *text;
                term_lines[term_cursor_row][term_cursor_col] = '\0';
            }
        }
        text++;
    }
}

static int str_prefix(const char* str, const char* prefix) {
    while (*prefix) {
        if (*str != *prefix) return 0;
        str++;
        prefix++;
    }
    return 1;
}

typedef int (*app_entry_fn)(os_api_t* api);

static void terminal_execute_command(const char* cmd) {
    if (cmd[0] == '>' && cmd[1] == ' ') cmd += 2;
    while (*cmd == ' ') cmd++;

    if (*cmd == '\0') return;

    if (str_prefix(cmd, "help")) {
        term_print("Commands:\n");
        term_print(" ls              List files\n");
        term_print(" cat <file>      Read file\n");
        term_print(" echo <t>><file> Write file\n");
        term_print(" view <file>     Open in Viewer\n");
        term_print(" ping <IPv4>     Ping four times\n");
        term_print(" clear           Clear screen\n");
    }
    else if (str_prefix(cmd, "clear")) {
        for (int r = 0; r < TERM_MAX_LINES; r++) {
            for (int c = 0; c <= TERM_MAX_COLS; c++) {
                term_lines[r][c] = 0;
            }
        }
        term_cursor_row = 0;
        term_cursor_col = 0;
    }
    else if (str_prefix(cmd, "ls") || str_prefix(cmd, "dir")) {
        char file_list[FS_MAX_FILES][FS_MAX_FILENAME];
        int count = fs_list(file_list);

        if (count == 0) {
            term_print("No files found.\n");
        } else {
            for (int i = 0; i < count; i++) {
                term_print(file_list[i]);
                term_print("\n");
            }
        }
    }
    else if (str_prefix(cmd, "cat ")) {
        const char* fname = cmd + 4;
        while (*fname == ' ') fname++;

        if (*fname == '\0') {
            term_print("Usage: cat <filename>\n");
        } else {
            char read_buf[512];
            int bytes = fs_read_file(fname, (uint8_t*)read_buf, 511);
            if (bytes < 0) {
                term_print("File not found!\n");
            } else {
                read_buf[bytes] = '\0';
                term_print(read_buf);
                term_print("\n");
            }
        }
    }
    else if (str_prefix(cmd, "view ")) {
        const char* fname = cmd + 5;
        while (*fname == ' ') fname++;

        if (*fname == '\0') {
            term_print("Usage: view <filename.bmp>\n");
        } else {
            if (viewer_load_bmp(fname)) {
                viewer_window.visible = 1;
                bring_to_front(&viewer_window);
                term_print("Loaded ");
                term_print(fname);
                term_print(" in Viewer\n");
            } else {
                term_print("Failed to load BMP!\n");
            }
        }
    }
    else if (str_prefix(cmd, "ping ")) {
        const char* target = cmd + 5;
        while (*target == ' ') target++;
        terminal_ping(target);
    }
    else if (str_prefix(cmd, "echo ")) {
        const char* p = cmd + 5;
        const char* redirect = 0;
        for (const char* s = p; *s; s++) {
            if (*s == '>') {
                redirect = s;
                break;
            }
        }

        if (!redirect) {
            term_print(p);
            term_print("\n");
        } else {
            char text_to_save[128];
            int text_len = (int)(redirect - p);
            if (text_len > 127) text_len = 127;
            for (int i = 0; i < text_len; i++) text_to_save[i] = p[i];
            while (text_len > 0 && text_to_save[text_len - 1] == ' ') text_len--;
            text_to_save[text_len] = '\0';

            const char* fname = redirect + 1;
            while (*fname == ' ') fname++;

            if (*fname == '\0') {
                term_print("Usage: echo <text> > <file>\n");
            } else {
                int written = fs_write_file(fname, (const uint8_t*)text_to_save, text_len);
                if (written >= 0) {
                    term_print("Saved to ");
                    term_print(fname);
                    term_print("\n");
                } else {
                    term_print("Write failed!\n");
                }
            }
        }
    }
    else {
        char prog_name[FS_MAX_FILENAME];
        int ci = 0;
        while (cmd[ci] && cmd[ci] != ' ' && ci < FS_MAX_FILENAME - 5) {
            prog_name[ci] = cmd[ci];
            ci++;
        }
        prog_name[ci] = '\0';

        uint8_t* app_target_mem = (uint8_t*)0x00200000;
        int bytes = fs_read_file(prog_name, app_target_mem, 16384);
        if (bytes <= 0) {
            prog_name[ci++] = '.';
            prog_name[ci++] = 'b';
            prog_name[ci++] = 'i';
            prog_name[ci++] = 'n';
            prog_name[ci]   = '\0';
            bytes = fs_read_file(prog_name, app_target_mem, 16384);
        }

        if (bytes > 0) {
            term_print("Running ");
            term_print(prog_name);
            term_print("...\n");

            // Direct call to app entry point linked at 0x00200000
            app_entry_fn entry = (app_entry_fn)0x00200000;
            entry(&kernel_api);

            needs_redraw = 1;
        } else {
            term_print("Unknown cmd. Type 'help'\n");
        }
    }
}

void terminal_draw(Window* win) {
    int32_t term_box_x = win->x + 6;
    int32_t term_box_y = win->y + 20;
    int32_t term_box_w = win->w - 12;
    int32_t term_box_h = win->h - 26;

    fill_rect(term_box_x, term_box_y, term_box_w, term_box_h, COLOR_BLACK);

    fill_rect(term_box_x - 1, term_box_y - 1, term_box_w + 2, 1, COLOR_DARK_GRAY);
    fill_rect(term_box_x - 1, term_box_y - 1, 1, term_box_h + 2, COLOR_DARK_GRAY);
    fill_rect(term_box_x - 1, term_box_y + term_box_h, term_box_w + 2, 1, COLOR_WHITE);
    fill_rect(term_box_x + term_box_w, term_box_y - 1, 1, term_box_h + 2, COLOR_WHITE);

    for (int r = 0; r < TERM_MAX_LINES; r++) {
        if (term_lines[r][0] != 0) {
            draw_string(term_box_x + 6, term_box_y + 6 + (r * 12), term_lines[r], COLOR_WHITE);
        }
    }

    if (win->focused && (frame_timer & 0x10)) {
        int caret_x = term_box_x + 6 + (term_cursor_col * 8);
        int caret_y = term_box_y + 6 + (term_cursor_row * 12);
        draw_char(caret_x, caret_y, '_', COLOR_WHITE, 1, 0);
    }
}

void terminal_handle_key(Window* win, char c) {
    (void)win;
    if (c == '\b') {
        if (term_cursor_col > 2) {
            term_cursor_col--;
            term_lines[term_cursor_row][term_cursor_col] = 0;
        }
    } else if (c == '\n') {
        char current_line[TERM_MAX_COLS + 1];
        for (int i = 0; i <= TERM_MAX_COLS; i++) {
            current_line[i] = term_lines[term_cursor_row][i];
        }

        if (term_cursor_row < TERM_MAX_LINES - 1) {
            term_cursor_row++;
        } else {
            term_scroll();
        }
        term_cursor_col = 0;
        term_lines[term_cursor_row][0] = '\0';

        terminal_execute_command(current_line);

        if (term_cursor_col > 0) {
            if (term_cursor_row < TERM_MAX_LINES - 1) {
                term_cursor_row++;
            } else {
                term_scroll();
            }
        }
        term_lines[term_cursor_row][0] = '>';
        term_lines[term_cursor_row][1] = ' ';
        term_lines[term_cursor_row][2] = '\0';
        term_cursor_col = 2;
    } else if (c >= ' ') {
        if (term_cursor_col < TERM_MAX_COLS) {
            term_lines[term_cursor_row][term_cursor_col++] = c;
            term_lines[term_cursor_row][term_cursor_col] = '\0';
        }
    }
}

void terminal_init(void) {
    terminal_window.x = 100;
    terminal_window.y = 50;
    terminal_window.w = 380;
    terminal_window.h = 240;
    terminal_window.title = "Terminal";
    terminal_window.visible = 1;
    terminal_window.focused = 1;
    terminal_window.draw_content = terminal_draw;
    terminal_window.handle_key = terminal_handle_key;

    for (int r = 0; r < TERM_MAX_LINES; r++) {
        for (int c = 0; c <= TERM_MAX_COLS; c++) {
            term_lines[r][c] = 0;
        }
    }
    term_lines[0][0] = '>';
    term_lines[0][1] = ' ';
    term_cursor_row = 0;
    term_cursor_col = 2;
}

void draw_taskbar(void) {
    fill_rect(0, TASKBAR_Y, SCREEN_WIDTH, TASKBAR_HEIGHT, COLOR_WIN_BG);
    fill_rect(0, TASKBAR_Y, SCREEN_WIDTH, 1, COLOR_WHITE);

    draw_button(4, TASKBAR_Y + 4, 60, 24, "Start", start_menu_open);

    draw_button(68,  TASKBAR_Y + 4, 80, 24, "Terminal", terminal_window.visible && terminal_window.focused);
    draw_button(152, TASKBAR_Y + 4, 80, 24, "Notepad",  notepad_window.visible && notepad_window.focused);
    draw_button(236, TASKBAR_Y + 4, 80, 24, "Viewer",   viewer_window.visible && viewer_window.focused);

    
    if (windows[3] && windows[3]->title) {
        draw_button(320, TASKBAR_Y + 4, 80, 24, "Files", windows[3]->visible && windows[3]->focused);
    }

    fill_rect(SCREEN_WIDTH - 64, TASKBAR_Y + 5, 58, 22, COLOR_WIN_BG);
    fill_rect(SCREEN_WIDTH - 64, TASKBAR_Y + 5, 58, 1, COLOR_DARK_GRAY);
    fill_rect(SCREEN_WIDTH - 64, TASKBAR_Y + 5, 1, 22, COLOR_DARK_GRAY);
    fill_rect(SCREEN_WIDTH - 64, TASKBAR_Y + 26, 58, 1, COLOR_WHITE);
    fill_rect(SCREEN_WIDTH - 7,  TASKBAR_Y + 5, 1, 22, COLOR_WHITE);
    draw_string(SCREEN_WIDTH - 52, TASKBAR_Y + 11, "12:00", COLOR_BLACK);

    if (start_menu_open) {
        int32_t menu_x = 4;
        int32_t menu_y = TASKBAR_Y - 96;
        int32_t menu_w = 120;
        int32_t menu_h = 92;

        fill_rect(menu_x, menu_y, menu_w, menu_h, COLOR_WIN_BG);
        fill_rect(menu_x, menu_y, menu_w, 1, COLOR_WHITE);
        fill_rect(menu_x, menu_y, 1, menu_h, COLOR_WHITE);
        fill_rect(menu_x, menu_y + menu_h - 1, menu_w, 1, COLOR_DARK_GRAY);
        fill_rect(menu_x + menu_w - 1, menu_y, 1, menu_h, COLOR_DARK_GRAY);

        draw_string(menu_x + 12, menu_y + 10, "Terminal", COLOR_BLACK);
        fill_rect(menu_x + 6, menu_y + 28, menu_w - 12, 1, COLOR_DARK_GRAY);
        fill_rect(menu_x + 6, menu_y + 29, menu_w - 12, 1, COLOR_WHITE);

        draw_string(menu_x + 12, menu_y + 38, "Notepad", COLOR_BLACK);
        fill_rect(menu_x + 6, menu_y + 56, menu_w - 12, 1, COLOR_DARK_GRAY);
        fill_rect(menu_x + 6, menu_y + 57, menu_w - 12, 1, COLOR_WHITE);

        draw_string(menu_x + 12, menu_y + 66, "Viewer", COLOR_BLACK);
    }
}

static void cursor_save(int32_t x, int32_t y) {
    uint32_t* buf = display_get_backbuffer();
    for (int r = 0; r < MOUSE_CURSOR_H; r++) {
        int py = y + r;
        if (py < 0 || py >= SCREEN_HEIGHT) continue;
        for (int c = 0; c < MOUSE_CURSOR_W; c++) {
            int px = x + c;
            if (px < 0 || px >= SCREEN_WIDTH) continue;
            cursor_saved_bg[r * MOUSE_CURSOR_W + c] = buf[py * SCREEN_WIDTH + px];
        }
    }
    cursor_bg_valid = 1;
}

static void cursor_restore(int32_t x, int32_t y) {
    if (!cursor_bg_valid) return;
    uint32_t* buf = display_get_backbuffer();
    for (int r = 0; r < MOUSE_CURSOR_H; r++) {
        int py = y + r;
        if (py < 0 || py >= SCREEN_HEIGHT) continue;
        for (int c = 0; c < MOUSE_CURSOR_W; c++) {
            int px = x + c;
            if (px < 0 || px >= SCREEN_WIDTH) continue;
            buf[py * SCREEN_WIDTH + px] = cursor_saved_bg[r * MOUSE_CURSOR_W + c];
        }
    }
    display_blit_rect(x, y, MOUSE_CURSOR_W, MOUSE_CURSOR_H);
}

static void update_cursor_fast(int32_t new_x, int32_t new_y) {
    if (new_x == old_mouse_x && new_y == old_mouse_y) return;

    cursor_restore(old_mouse_x, old_mouse_y);
    cursor_save(new_x, new_y);
    draw_cursor(new_x, new_y);
    display_blit_rect(new_x, new_y, MOUSE_CURSOR_W, MOUSE_CURSOR_H);

    old_mouse_x = new_x;
    old_mouse_y = new_y;
}

static void render_screen(void) {
    display_clear(COLOR_DESKTOP);
    desktop_draw();

    for (int i = 0; i < NUM_WINDOWS; i++) {
        if (windows[i]) {
            draw_window_frame(windows[i]);
        }
    }

    draw_taskbar();

    cursor_save(mouse_x, mouse_y);
    draw_cursor(mouse_x, mouse_y);
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;

    display_blit();
}

static int ps2_wait_write(void) {
    for (volatile int i = 0; i < 2000; i++) {
        if (!(inb(0x64) & 0x02)) return 1;
    }
    return 0;
}

static int ps2_wait_read(void) {
    for (volatile int i = 0; i < 2000; i++) {
        if (inb(0x64) & 0x01) return 1;
    }
    return 0;
}

static void ps2_flush_buffer(void) {
    for (volatile int i = 0; i < 100; i++) {
        if (inb(0x64) & 0x01) {
            inb(0x60);
        } else {
            break;
        }
    }
}

static void ps2_send_mouse_cmd(uint8_t cmd) {
    if (!ps2_wait_write()) return;
    outb(0x64, 0xD4);

    if (!ps2_wait_write()) return;
    outb(0x60, cmd);

    if (ps2_wait_read()) {
        inb(0x60);
    }
}

void init_mouse(void) {
    ps2_flush_buffer();

    if (ps2_wait_write()) outb(0x64, 0xA8);
    if (ps2_wait_write()) outb(0x64, 0x20);

    if (ps2_wait_read()) {
        uint8_t config = inb(0x60);
        config |= 0x02;
        config &= ~0x20;

        if (ps2_wait_write()) outb(0x64, 0x60);
        if (ps2_wait_write()) outb(0x60, config);
    }

    ps2_send_mouse_cmd(0xF6);
    ps2_send_mouse_cmd(0xF4);

    ps2_flush_buffer();
}

void kernel_main(uint32_t lfb_address, uint32_t bpp, uint32_t pitch) {
    __asm__ volatile ("cli");
    
    display_init(lfb_address, bpp, pitch);
    idt_init();
    pit_init(100);
    net_init(0xC100);
    {
        uint64_t mac_value = net_get_mac_address();
        uint8_t mac[6];
        for (int i = 5; i >= 0; i--) {
            mac[i] = (uint8_t)mac_value;
            mac_value >>= 8;
        }
        ipv4_init(0x0A00020F, mac, 0x0A000202, 0xFFFFFF00);
        arp_init(0x0A00020F, mac);
    }
    icmp_init();
    icmp_set_echo_reply_handler(ping_reply);
    fs_init();
    tasking_init();

    terminal_init();
    notepad_init();
    viewer_init();
    dialog_init(&kernel_api);

    // fuckass line was breaking shit
    //outb(0x21, inb(0x21) & ~0x01);

    windows[0] = &notepad_window;
    windows[1] = &viewer_window;
    windows[2] = &terminal_window;

    desktop_init(&terminal_window, &notepad_window);
    init_mouse();

    render_screen();
    needs_redraw = 0;

    __asm__ volatile ("sti");

    while (1) {
        frame_timer++;
        net_poll();

        if ((frame_timer & 0x3F) == 0 && terminal_window.visible && terminal_window.focused) {
            needs_redraw = 1;
        }

        uint8_t status = inb(0x64);
        if (status & 0x01) {
            if (status & 0x20) {
                uint8_t byte = inb(0x60);

                if (mouse_cycle == 0) {
                    if (byte & 0x08) {
                        mouse_packet[0] = byte;
                        mouse_cycle = 1;
                    }
                } else if (mouse_cycle == 1) {
                    mouse_packet[1] = byte;
                    mouse_cycle = 2;
                } else if (mouse_cycle == 2) {
                    mouse_packet[2] = byte;
                    mouse_cycle = 0;

                    uint8_t flags = mouse_packet[0];
                    int8_t dx = (int8_t)mouse_packet[1];
                    int8_t dy = (int8_t)mouse_packet[2];

                    if (!(flags & 0xC0)) {
                        if (dx != 0 || dy != 0) {
                            mouse_x += dx;
                            mouse_y -= dy;

                            if (mouse_x < 0) mouse_x = 0;
                            if (mouse_x > SCREEN_WIDTH - 12) mouse_x = SCREEN_WIDTH - 12;
                            if (mouse_y < 0) mouse_y = 0;
                            if (mouse_y > SCREEN_HEIGHT - 19) mouse_y = SCREEN_HEIGHT - 19;

                            if (dragging_window) {
                                needs_redraw = 1;
                            } else {
                                update_cursor_fast(mouse_x, mouse_y);
                            }
                        }

                        uint8_t left_click = flags & 0x01;
                        uint8_t click_down = left_click && !prev_left_click;

                        if (left_click != prev_left_click) {
                            needs_redraw = 1;
                        }

                        if (left_click) {
                            if (dragging_window) {
                                dragging_window->x += dx;
                                dragging_window->y -= dy;
                                if (dragging_window->y < 0) dragging_window->y = 0;
                                if (dragging_window->y > TASKBAR_Y - 24) dragging_window->y = TASKBAR_Y - 24;
                                needs_redraw = 1;
                            } else if (click_down) {
                                uint8_t handled = 0;

                                if (mouse_x >= 4 && mouse_x <= 64 &&
                                    mouse_y >= TASKBAR_Y + 4 && mouse_y <= TASKBAR_Y + 28) 
                                {
                                    start_menu_open = !start_menu_open;
                                    handled = 1;
                                    needs_redraw = 1;
                                }
                                else if (mouse_x >= 68 && mouse_x <= 148 &&
                                         mouse_y >= TASKBAR_Y + 4 && mouse_y <= TASKBAR_Y + 28) 
                                {
                                    terminal_window.visible = !terminal_window.visible;
                                    if (terminal_window.visible) bring_to_front(&terminal_window);
                                    start_menu_open = 0;
                                    handled = 1;
                                    needs_redraw = 1;
                                }
                                else if (mouse_x >= 152 && mouse_x <= 232 &&
                                         mouse_y >= TASKBAR_Y + 4 && mouse_y <= TASKBAR_Y + 28) 
                                {
                                    notepad_window.visible = !notepad_window.visible;
                                    if (notepad_window.visible) bring_to_front(&notepad_window);
                                    start_menu_open = 0;
                                    handled = 1;
                                    needs_redraw = 1;
                                }
                                else if (mouse_x >= 236 && mouse_x <= 316 &&
                                         mouse_y >= TASKBAR_Y + 4 && mouse_y <= TASKBAR_Y + 28) 
                                {
                                    viewer_window.visible = !viewer_window.visible;
                                    if (viewer_window.visible) bring_to_front(&viewer_window);
                                    start_menu_open = 0;
                                    handled = 1;
                                    needs_redraw = 1;
                                }
                                else if (windows[3] && mouse_x >= 320 && mouse_x <= 400 &&
                                         mouse_y >= TASKBAR_Y + 4 && mouse_y <= TASKBAR_Y + 28)
                                {
                                    windows[3]->visible = !windows[3]->visible;
                                    if (windows[3]->visible) bring_to_front(windows[3]);
                                    start_menu_open = 0;
                                    handled = 1;
                                    needs_redraw = 1;
                                }
                                else if (start_menu_open && mouse_x >= 4 && mouse_x <= 124 &&
                                         mouse_y >= (TASKBAR_Y - 96) && mouse_y < TASKBAR_Y) 
                                {
                                    if (mouse_y < TASKBAR_Y - 64) {
                                        terminal_window.visible = 1;
                                        bring_to_front(&terminal_window);
                                    } else if (mouse_y < TASKBAR_Y - 32) {
                                        notepad_window.visible = 1;
                                        bring_to_front(&notepad_window);
                                    } else {
                                        viewer_window.visible = 1;
                                        bring_to_front(&viewer_window);
                                    }
                                    start_menu_open = 0;
                                    handled = 1;
                                    needs_redraw = 1;
                                }

                                if (!handled) {
                                    start_menu_open = 0;
                                    for (int i = NUM_WINDOWS - 1; i >= 0; i--) {
                                        Window* w = windows[i];
                                        if (!w || !w->visible) continue;

                                        int32_t btn_x = w->x + w->w - 16;
                                        int32_t btn_y = w->y + 3;
                                        if (mouse_x >= btn_x && mouse_x <= btn_x + 13 &&
                                            mouse_y >= btn_y && mouse_y <= btn_y + 13) 
                                        {
                                            w->visible = 0;
                                            handled = 1;
                                            needs_redraw = 1;
                                            break;
                                        }

                                        if (mouse_x >= w->x && mouse_x <= (w->x + w->w) &&
                                            mouse_y >= w->y && mouse_y <= (w->y + 18)) 
                                        {
                                            bring_to_front(w);
                                            dragging_window = w;
                                            handled = 1;
                                            needs_redraw = 1;
                                            break;
                                        }

                                        if (mouse_x >= w->x && mouse_x <= (w->x + w->w) &&
                                            mouse_y >= w->y && mouse_y <= (w->y + w->h)) 
                                        {
                                            bring_to_front(w);
                                            handled = 1;
                                            needs_redraw = 1;
                                            if (w->handle_mouse) {
                                                w->handle_mouse(w, mouse_x - w->x, mouse_y - w->y, left_click);
                                            }
                                            break;
                                        }
                                    }
                                }

                                if (!handled && mouse_y < TASKBAR_Y) {
                                    if (desktop_handle_click(mouse_x, mouse_y, frame_timer)) {
                                        needs_redraw = 1;
                                    }
                                }
                            }
                        } else {
                            dragging_window = 0;
                        }

                        prev_left_click = left_click;
                    }
                }
            } else {
                uint8_t scancode = inb(0x60);

                if (scancode == 0x3C || scancode == 0x3D || scancode == 0x48 || scancode == 0x50 || scancode == 0x01) {
                    for (int i = 0; i < NUM_WINDOWS; i++) {
                        if (windows[i] && windows[i]->visible && windows[i]->focused && windows[i]->handle_key) {
                            windows[i]->handle_key(windows[i], (char)scancode);
                            needs_redraw = 1;
                            break;
                        }
                    }
                } else {
                    char ascii = kbd_scancode_to_ascii(scancode);
                    if (ascii != 0) {
                        for (int i = 0; i < NUM_WINDOWS; i++) {
                            if (windows[i] && windows[i]->visible && windows[i]->focused && windows[i]->handle_key) {
                                windows[i]->handle_key(windows[i], ascii);
                                needs_redraw = 1;
                                break;
                            }
                        }
                    }
                }
            }
        }

        if (needs_redraw) {
            render_screen();
            needs_redraw = 0;
        }
    }
}
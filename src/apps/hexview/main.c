#include "../../kernel/types.h"
#include "../../kernel/os_api.h"
#include "../../kernel/window.h"

#define COLOR_WIN_BG      0x00C0C0C0
#define COLOR_DARK_GRAY   0x00808080
#define COLOR_LIGHT_GRAY  0x00DFDFDF
#define COLOR_WHITE       0x00FFFFFF
#define COLOR_BLACK       0x00000000
#define COLOR_HEADER_BG   0x00000080
#define COLOR_ADDR_TEXT   0x00808000
#define COLOR_HEX_TEXT    0x00FFFFFF
#define COLOR_ASCII_TEXT  0x0000FF00

#define MAX_FILE_SIZE     16384
#define BYTES_PER_ROW     16
#define VISIBLE_ROWS      14

static os_api_t* os = 0;
static Window hex_window;

static char target_file[FS_MAX_FILENAME] = "calc.bin";
static uint8_t file_buffer[MAX_FILE_SIZE];
static int total_bytes = 0;
static int current_offset = 0;

static const char hex_chars[] = "0123456789ABCDEF";

static void byte_to_hex(uint8_t byte, char* out) {
    out[0] = hex_chars[(byte >> 4) & 0x0F];
    out[1] = hex_chars[byte & 0x0F];
}

static void u16_to_hex(uint16_t val, char* out) {
    out[0] = hex_chars[(val >> 12) & 0x0F];
    out[1] = hex_chars[(val >> 8) & 0x0F];
    out[2] = hex_chars[(val >> 4) & 0x0F];
    out[3] = hex_chars[val & 0x0F];
}

static void load_current_file(void) {
    for (int i = 0; i < MAX_FILE_SIZE; i++) {
        file_buffer[i] = 0;
    }
    total_bytes = os->fs_read(target_file, file_buffer, MAX_FILE_SIZE);
    if (total_bytes < 0) total_bytes = 0;
    current_offset = 0;
}

static void draw_sunken_panel(int32_t x, int32_t y, int32_t w, int32_t h) {
    os->fill_rect(x, y, w, h, COLOR_BLACK);
    os->fill_rect(x - 1, y - 1, w + 2, 1, COLOR_DARK_GRAY);
    os->fill_rect(x - 1, y - 1, 1, h + 2, COLOR_DARK_GRAY);
    os->fill_rect(x - 1, y + h, w + 2, 1, COLOR_WHITE);
    os->fill_rect(x + w, y - 1, 1, h + 2, COLOR_WHITE);
}

void hex_draw_content(Window* win) {
    os->draw_string(win->x + 10, win->y + 24, "File: ", COLOR_BLACK);
    os->draw_string(win->x + 50, win->y + 24, target_file, COLOR_BLACK);

    os->draw_button(win->x + win->w - 130, win->y + 22, 55, 18, "Prev", 0);
    os->draw_button(win->x + win->w - 70, win->y + 22, 55, 18, "Next", 0);

    int32_t panel_x = win->x + 10;
    int32_t panel_y = win->y + 44;
    int32_t panel_w = win->w - 20;
    int32_t panel_h = win->h - 52;
    draw_sunken_panel(panel_x, panel_y, panel_w, panel_h);

    os->draw_string(panel_x + 6, panel_y + 4, "OFFSET  00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F  ASCII", 0x00A0A0A0);

    char offset_str[6] = "0000:";
    char hex_str[4] = "00 ";
    char ascii_str[2] = ".";

    for (int r = 0; r < VISIBLE_ROWS; r++) {
        int row_offset = current_offset + (r * BYTES_PER_ROW);
        if (row_offset >= total_bytes && total_bytes > 0) break;

        int row_y = panel_y + 18 + (r * 11);

        u16_to_hex((uint16_t)row_offset, offset_str);
        os->draw_string(panel_x + 6, row_y, offset_str, COLOR_ADDR_TEXT);

        for (int c = 0; c < BYTES_PER_ROW; c++) {
            int byte_idx = row_offset + c;
            int hx = panel_x + 54 + (c * 18);

            if (byte_idx < total_bytes) {
                byte_to_hex(file_buffer[byte_idx], hex_str);
                os->draw_string(hx, row_y, hex_str, COLOR_HEX_TEXT);
            }
        }

        int ascii_x_start = panel_x + 54 + (16 * 18) + 8;
        for (int c = 0; c < BYTES_PER_ROW; c++) {
            int byte_idx = row_offset + c;
            if (byte_idx < total_bytes) {
                uint8_t ch = file_buffer[byte_idx];
                ascii_str[0] = (ch >= 32 && ch <= 126) ? (char)ch : '.';
                os->draw_string(ascii_x_start + (c * 8), row_y, ascii_str, COLOR_ASCII_TEXT);
            }
        }
    }
}

void hex_handle_mouse(Window* win, int32_t lx, int32_t ly, uint8_t btn) {
    (void)btn;
    if (lx >= win->w - 130 && lx <= win->w - 75 && ly >= 22 && ly <= 40) {
        if (current_offset >= (VISIBLE_ROWS * BYTES_PER_ROW)) {
            current_offset -= (VISIBLE_ROWS * BYTES_PER_ROW);
        } else {
            current_offset = 0;
        }
        hex_draw_content(win);
        os->blit_all();
        return;
    }

    if (lx >= win->w - 70 && lx <= win->w - 15 && ly >= 22 && ly <= 40) {
        if (current_offset + (VISIBLE_ROWS * BYTES_PER_ROW) < total_bytes) {
            current_offset += (VISIBLE_ROWS * BYTES_PER_ROW);
        }
        hex_draw_content(win);
        os->blit_all();
        return;
    }
}

void hex_handle_key(Window* win, char c) {
    if ((uint8_t)c == 0x01) {
        os->unregister_window(win);
        return;
    }

    if (c == ' ' || c == 'j' || c == 's') {
        if (current_offset + (VISIBLE_ROWS * BYTES_PER_ROW) < total_bytes) {
            current_offset += (VISIBLE_ROWS * BYTES_PER_ROW);
        }
        hex_draw_content(win);
        os->blit_all();
        return;
    }

    if (c == 'k' || c == 'w') {
        if (current_offset >= (VISIBLE_ROWS * BYTES_PER_ROW)) {
            current_offset -= (VISIBLE_ROWS * BYTES_PER_ROW);
        } else {
            current_offset = 0;
        }
        hex_draw_content(win);
        os->blit_all();
        return;
    }

    if (c == '1') {
        char name[] = "files.bin";
        for (int i = 0; (target_file[i] = name[i]); i++);
        load_current_file();
        hex_draw_content(win);
        os->blit_all();
    } else if (c == '2') {
        char name[] = "mines.bin";
        for (int i = 0; (target_file[i] = name[i]); i++);
        load_current_file();
        hex_draw_content(win);
        os->blit_all();
    } else if (c == '3') {
        char name[] = "calc.bin";
        for (int i = 0; (target_file[i] = name[i]); i++);
        load_current_file();
        hex_draw_content(win);
        os->blit_all();
    }
}

int main_app(os_api_t* api) {
    if (!api) return -1;
    os = api;

    load_current_file();

    hex_window.x = 120;
    hex_window.y = 80;
    hex_window.w = 510;
    hex_window.h = 220;
    hex_window.title = "Hex Viewer";
    hex_window.visible = 1;
    hex_window.focused = 1;
    hex_window.draw_content = hex_draw_content;
    hex_window.handle_key = hex_handle_key;
    hex_window.handle_mouse = hex_handle_mouse;

    os->register_window(&hex_window);
    os->show_info_dialog("Hex Viewer", "Hex Viewer is running.");
    return 0;
}
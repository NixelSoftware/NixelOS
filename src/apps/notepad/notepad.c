#include "notepad.h"
#include "../../kernel/drivers/display/font.h"
#include "../../kernel/drivers/display.h"
#include "../../kernel/fs.h"

Window notepad_window;

#define NOTEPAD_MAX_CHARS 256
static char notepad_buffer[NOTEPAD_MAX_CHARS];
static int notepad_cursor = 0;

void notepad_draw(Window* win) {
    int32_t box_x = win->x + 6;
    int32_t box_y = win->y + 20;
    int32_t box_w = win->w - 12;
    int32_t box_h = win->h - 26;

    display_fill_rect(box_x, box_y, box_w, box_h, COLOR_WHITE);

    display_fill_rect(box_x - 1, box_y - 1, box_w + 2, 1, COLOR_DARK_GRAY);
    display_fill_rect(box_x - 1, box_y - 1, 1, box_h + 2, COLOR_DARK_GRAY);
    display_fill_rect(box_x - 1, box_y + box_h, box_w + 2, 1, COLOR_WHITE);
    display_fill_rect(box_x + box_w, box_y - 1, 1, box_h + 2, COLOR_WHITE);

    draw_string(box_x + 6, box_y + 6, notepad_buffer, COLOR_BLACK);

    if (win->focused) {
        int cursor_column = 0;
        int cursor_row = 0;
        for (int i = 0; i < notepad_cursor; i++) {
            if (notepad_buffer[i] == '\n') {
                cursor_column = 0;
                cursor_row++;
            } else {
                cursor_column++;
            }
        }

        int caret_x = box_x + 6 + (cursor_column * 8);
        int caret_y = box_y + 6 + (cursor_row * 10);
        draw_char(caret_x, caret_y, '|', COLOR_BLACK, 1, 0);
    }
}

void notepad_handle_key(Window* win, char c) {
    (void)win;

    
    if ((uint8_t)c == 0x3C) {
        fs_write_file("note.txt", (const uint8_t*)notepad_buffer, notepad_cursor);
        return;
    }

    
    if ((uint8_t)c == 0x3D) {
        int bytes = fs_read_file("note.txt", (uint8_t*)notepad_buffer, NOTEPAD_MAX_CHARS - 1);
        if (bytes >= 0) {
            notepad_cursor = bytes;
            notepad_buffer[notepad_cursor] = '\0';
        }
        return;
    }

    if (c == '\b') {
        if (notepad_cursor > 0) {
            notepad_cursor--;
            notepad_buffer[notepad_cursor] = '\0';
        }
    } else if (c == '\n') {
        if (notepad_cursor < NOTEPAD_MAX_CHARS - 1) {
            notepad_buffer[notepad_cursor++] = c;
            notepad_buffer[notepad_cursor] = '\0';
        }
    } else if (c >= ' ' && c <= '~') {
        if (notepad_cursor < NOTEPAD_MAX_CHARS - 1) {
            notepad_buffer[notepad_cursor++] = c;
            notepad_buffer[notepad_cursor] = '\0';
        }
    }
}

void notepad_init(void) {
    notepad_window.x = 220;
    notepad_window.y = 120;
    notepad_window.w = 340;
    notepad_window.h = 180;
    notepad_window.title = "Notepad";
    notepad_window.visible = 1;
    notepad_window.focused = 1;
    notepad_window.draw_content = notepad_draw;
    notepad_window.handle_key = notepad_handle_key;

    for (int i = 0; i < NOTEPAD_MAX_CHARS; i++) {
        notepad_buffer[i] = 0;
    }
    notepad_cursor = 0;
}
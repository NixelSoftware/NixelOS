#include "notepad.h"
#include "../../kernel/mutex.h"
#include "../../system/graphics/wm/wm.h"
#include "../../kernel/drivers/display/font.h"
#include "../../kernel/drivers/display.h"
#include "../../kernel/fs.h"

#define NOTEPAD_MAX_CHARS 256
#define NOTEPAD_MAX_INSTANCES 4
typedef struct {
    Window window;
    char title[24];
    char buffer[NOTEPAD_MAX_CHARS];
    int cursor;
    uint8_t active;
} notepad_t;
static notepad_t notes[NOTEPAD_MAX_INSTANCES];
static notepad_t* note_for(Window* window) {
    for (int i = 0; i < NOTEPAD_MAX_INSTANCES; i++)
        if (notes[i].active && &notes[i].window == window) return &notes[i];
    return 0;
}
void notepad_draw(Window* win) {
    notepad_t* note = note_for(win);
    if (!note) return;
    int32_t box_x = win->x + 6;
    int32_t box_y = win->y + 20;
    int32_t box_w = win->w - 12;
    int32_t box_h = win->h - 26;

    display_fill_rect(box_x, box_y, box_w, box_h, COLOR_WHITE);

    display_fill_rect(box_x - 1, box_y - 1, box_w + 2, 1, COLOR_DARK_GRAY);
    display_fill_rect(box_x - 1, box_y - 1, 1, box_h + 2, COLOR_DARK_GRAY);
    display_fill_rect(box_x - 1, box_y + box_h, box_w + 2, 1, COLOR_WHITE);
    display_fill_rect(box_x + box_w, box_y - 1, 1, box_h + 2, COLOR_WHITE);

    draw_string(box_x + 6, box_y + 6, note->buffer, COLOR_BLACK);

    if (win->focused) {
        int cursor_column = 0;
        int cursor_row = 0;
        for (int i = 0; i < note->cursor; i++) {
            if (note->buffer[i] == '\n') {
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
    notepad_t* note = note_for(win);
    if (!note) return;

    
    if ((uint8_t)c == 0x3C) {
        fs_write_file("note.txt", (const uint8_t*)note->buffer, note->cursor);
        return;
    }

    
    if ((uint8_t)c == 0x3D) {
        int bytes = fs_read_file("note.txt", (uint8_t*)note->buffer, NOTEPAD_MAX_CHARS - 1);
        if (bytes >= 0) {
            note->cursor = bytes;
            note->buffer[note->cursor] = '\0';
        }
        return;
    }

    if (c == '\b') {
        if (note->cursor > 0) {
            note->cursor--;
            note->buffer[note->cursor] = '\0';
        }
    } else if (c == '\n') {
        if (note->cursor < NOTEPAD_MAX_CHARS - 1) {
            note->buffer[note->cursor++] = c;
            note->buffer[note->cursor] = '\0';
        }
    } else if (c >= ' ' && c <= '~') {
        if (note->cursor < NOTEPAD_MAX_CHARS - 1) {
            note->buffer[note->cursor++] = c;
            note->buffer[note->cursor] = '\0';
        }
    }
}

void notepad_init(void) {}
int notepad_close(Window* window) {
    MUTEX_GUARD(kernel_mutex);
    notepad_t* note = note_for(window);
    if (!note) return 0;
    wm_unregister_window(window);
    window->visible = 0;
    return 1;
}
static void notepad_service(void* argument) {
    notepad_t* note = argument;
    for (;;) {
        mutex_lock(&kernel_mutex);
        if (!note->window.visible || current_task->terminate_requested) {
            wm_unregister_window(&note->window);
            note->active = 0;
            mutex_unlock(&kernel_mutex);
            return;
        }
        mutex_unlock(&kernel_mutex);
        task_sleep_ms(10);
    }
}
int notepad_launch(void) {
    MUTEX_GUARD(kernel_mutex);
    for (int i = 0; i < NOTEPAD_MAX_INSTANCES; i++) {
        notepad_t* note = &notes[i];
        if (note->active) continue;
        *note = (notepad_t){0};
        note->active = 1;
        const char* title = "Notepad";
        int n = 0;
        while (title[n]) { note->title[n] = title[n]; n++; }
        note->title[n] = 0;
        note->window = (Window){
            .x = 220 + i * 24, .y = 120 + i * 24, .w = 340, .h = 180,
            .title = note->title, .visible = 1, .focused = 1,
            .draw_content = notepad_draw, .handle_key = notepad_handle_key
        };
        if (wm_register_window(&note->window) < 0) { note->active = 0; return -1; }
        if (task_create(notepad_service, note, "notepad") < 0) {
            wm_unregister_window(&note->window); note->active = 0; return -1;
        }
        return i;
    }
    return -1;
}

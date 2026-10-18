#include "../../kernel/types.h"
#include "../../kernel/os_api.h"
#include "../../kernel/fs.h"
#include "../../kernel/window.h"

#define COLOR_WIN_BG    0x00C0C0C0
#define COLOR_DARK_GRAY 0x00808080
#define COLOR_WHITE     0x00FFFFFF
#define COLOR_BLACK     0x00000000
#define COLOR_SELECT_BG 0x000000AA

static os_api_t* os = 0;
static Window files_window;
static char file_list[FS_MAX_FILES][FS_MAX_FILENAME];
static int  file_count = 0;
static int  selected_file = 0;

static void itoa(int n, char* buf) {
    if (n == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    char temp[12];
    int i = 0;
    while (n > 0) {
        temp[i++] = (n % 10) + '0';
        n /= 10;
    }
    int j = 0;
    while (i > 0) {
        buf[j++] = temp[--i];
    }
    buf[j] = '\0';
}

void files_draw_content(Window* win) {
    int32_t box_x = win->x + 8;
    int32_t box_y = win->y + 24;
    int32_t box_w = win->w - 16;
    int32_t box_h = win->h - 60;

    
    os->fill_rect(box_x, box_y, box_w, box_h, COLOR_WHITE);
    os->fill_rect(box_x - 1, box_y - 1, box_w + 2, 1, COLOR_DARK_GRAY);
    os->fill_rect(box_x - 1, box_y - 1, 1, box_h + 2, COLOR_DARK_GRAY);
    os->fill_rect(box_x - 1, box_y + box_h, box_w + 2, 1, COLOR_WHITE);
    os->fill_rect(box_x + box_w, box_y - 1, 1, box_h + 2, COLOR_WHITE);

    
    os->draw_string(box_x + 6, box_y + 4, "Name", COLOR_DARK_GRAY);
    os->draw_string(box_x + 180, box_y + 4, "Size", COLOR_DARK_GRAY);
    os->fill_rect(box_x + 4, box_y + 16, box_w - 8, 1, COLOR_DARK_GRAY);

    
    for (int i = 0; i < file_count && i < 10; i++) {
        int row_y = box_y + 20 + (i * 14);

        if (i == selected_file) {
            os->fill_rect(box_x + 2, row_y - 2, box_w - 4, 14, COLOR_SELECT_BG);
            os->draw_string(box_x + 6, row_y, file_list[i], COLOR_WHITE);
        } else {
            os->draw_string(box_x + 6, row_y, file_list[i], COLOR_BLACK);
        }

        uint8_t dummy[1];
        int size = os->fs_read(file_list[i], dummy, 0);
        if (size >= 0) {
            char size_str[16];
            itoa(size, size_str);
            int len = 0;
            while (size_str[len]) len++;
            size_str[len++] = ' ';
            size_str[len++] = 'B';
            size_str[len] = '\0';
            os->draw_string(box_x + 180, row_y, size_str, (i == selected_file) ? COLOR_WHITE : COLOR_DARK_GRAY);
        }
    }

    
    char count_str[16];
    itoa(file_count, count_str);
    os->draw_string(win->x + 10, win->y + win->h - 24, "Total files: ", COLOR_BLACK);
    os->draw_string(win->x + 110, win->y + win->h - 24, count_str, COLOR_BLACK);
}

void files_handle_key(Window* win, char c) {
    (void)win;
    if ((uint8_t)c == 0x50) { 
        if (selected_file < file_count - 1) {
            selected_file++;
        }
    } else if ((uint8_t)c == 0x48) { 
        if (selected_file > 0) {
            selected_file--;
        }
    } else if ((uint8_t)c == 0x01) { 
        os->unregister_window(&files_window);
    }
}


int main_app(os_api_t* api) {
    if (!api) return -1;
    os = api;

    file_count = os->fs_list(file_list);
    selected_file = 0;

    
    files_window.x = 220;
    files_window.y = 120;
    files_window.w = 360;
    files_window.h = 240;
    files_window.title = "File Explorer";
    files_window.visible = 1;
    files_window.focused = 1;
    files_window.draw_content = files_draw_content;
    files_window.handle_key = files_handle_key;

    
    os->register_window(&files_window);
    os->term_print("File Explorer is running.\n");

    return 0;
}
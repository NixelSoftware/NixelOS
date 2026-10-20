#include "../../kernel/types.h"
#include "../../kernel/os_api.h"
#include "../../kernel/fs.h"
#include "../../kernel/window.h"
#include "../../system/user/sys_api.h"

#define COLOR_WIN_BG    0x00C0C0C0
#define COLOR_DARK_GRAY 0x00808080
#define COLOR_WHITE     0x00FFFFFF
#define COLOR_BLACK     0x00000000
#define COLOR_SELECT_BG 0x000000AA

static os_api_t user_api;
static os_api_t* os = &user_api;
static Window files_window;
static uint32_t files_window_handle;
static uint8_t files_close_requested;
static char file_list[FS_MAX_FILES][FS_MAX_FILENAME];
static int file_sizes[FS_MAX_FILES];
static int  file_count = 0;
static int  selected_file = 0;

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
    files_window_handle = (uint32_t)handle;
    return 0;
}

static void user_unregister_window(Window* window) {
    (void)window;
    if (files_window_handle) {
        (void)sys_window_close(files_window_handle);
        files_window_handle = 0;
    }
}

static void user_fill_rect(int32_t x, int32_t y, int32_t width, int32_t height,
                           uint32_t color) {
    user_window_draw_t drawing = {0};
    drawing.operation = USER_WINDOW_DRAW_FILL;
    drawing.x = x - files_window.x;
    drawing.y = y - files_window.y;
    drawing.width = width;
    drawing.height = height;
    drawing.color = color;
    (void)sys_window_draw(files_window_handle, &drawing);
}

static void user_draw_string(int32_t x, int32_t y, const char* text,
                             uint32_t color) {
    user_window_draw_t drawing = {0};
    drawing.operation = USER_WINDOW_DRAW_TEXT;
    drawing.x = x - files_window.x;
    drawing.y = y - files_window.y;
    drawing.text = text;
    drawing.color = color;
    (void)sys_window_draw(files_window_handle, &drawing);
}

static void user_blit_all(void) {
    if (files_window_handle) (void)sys_window_present(files_window_handle);
}

static void load_file_list(void) {
    file_count = sys_fs_list(file_list);
    if (file_count < 0) file_count = 0;
    if (file_count > FS_MAX_FILES) file_count = FS_MAX_FILES;
    for (int i = 0; i < file_count; i++)
        file_sizes[i] = sys_fs_read(file_list[i], 0, 0);
    selected_file = 0;
}

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

    
    os->graphics.fill_rect(box_x, box_y, box_w, box_h, COLOR_WHITE);
    os->graphics.fill_rect(box_x - 1, box_y - 1, box_w + 2, 1, COLOR_DARK_GRAY);
    os->graphics.fill_rect(box_x - 1, box_y - 1, 1, box_h + 2, COLOR_DARK_GRAY);
    os->graphics.fill_rect(box_x - 1, box_y + box_h, box_w + 2, 1, COLOR_WHITE);
    os->graphics.fill_rect(box_x + box_w, box_y - 1, 1, box_h + 2, COLOR_WHITE);

    
    os->graphics.draw_string(box_x + 6, box_y + 4, "Name", COLOR_DARK_GRAY);
    os->graphics.draw_string(box_x + 180, box_y + 4, "Size", COLOR_DARK_GRAY);
    os->graphics.fill_rect(box_x + 4, box_y + 16, box_w - 8, 1, COLOR_DARK_GRAY);

    
    for (int i = 0; i < file_count && i < 10; i++) {
        int row_y = box_y + 20 + (i * 14);

        if (i == selected_file) {
            os->graphics.fill_rect(box_x + 2, row_y - 2, box_w - 4, 14, COLOR_SELECT_BG);
            os->graphics.draw_string(box_x + 6, row_y, file_list[i], COLOR_WHITE);
        } else {
            os->graphics.draw_string(box_x + 6, row_y, file_list[i], COLOR_BLACK);
        }

        int size = file_sizes[i];
        if (size >= 0) {
            char size_str[16];
            itoa(size, size_str);
            int len = 0;
            while (size_str[len]) len++;
            size_str[len++] = ' ';
            size_str[len++] = 'B';
            size_str[len] = '\0';
            os->graphics.draw_string(box_x + 180, row_y, size_str, (i == selected_file) ? COLOR_WHITE : COLOR_DARK_GRAY);
        }
    }

    
    char count_str[16];
    itoa(file_count, count_str);
    os->graphics.draw_string(win->x + 10, win->y + win->h - 24, "Total files: ", COLOR_BLACK);
    os->graphics.draw_string(win->x + 110, win->y + win->h - 24, count_str, COLOR_BLACK);
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
        files_close_requested = 1;
    }
}

void files_handle_mouse(Window* win, int32_t lx, int32_t ly, uint8_t buttons) {
    (void)buttons;
    if (lx < 8 || lx >= win->w - 8 || ly < 44) return;
    int row = (ly - 44) / 14;
    if (row >= 0 && row < file_count && row < 10) selected_file = row;
}

void user_main(const char* arguments) {
    (void)arguments;
    files_window_handle = 0;
    files_close_requested = 0;
    user_api.graphics.fill_rect = user_fill_rect;
    user_api.graphics.draw_string = user_draw_string;
    user_api.graphics.blit_all = user_blit_all;
    user_api.windows.register_window = user_register_window;
    user_api.windows.unregister_window = user_unregister_window;
    load_file_list();

    
    files_window.x = 220;
    files_window.y = 120;
    files_window.w = 360;
    files_window.h = 240;
    files_window.title = "File Explorer";
    files_window.visible = 1;
    files_window.focused = 1;
    files_window.draw_content = files_draw_content;
    files_window.handle_key = files_handle_key;
    files_window.handle_mouse = files_handle_mouse;

    
    if (os->windows.register_window(&files_window) < 0) {
        static const char error[] = "files: unable to create window\n";
        sys_write(error, sizeof(error) - 1u);
        return;
    }
    static const char started[] = "File Explorer is running.\n";
    sys_write(started, sizeof(started) - 1u);

    files_draw_content(&files_window);
    os->graphics.blit_all();
    while (!files_close_requested) {
        user_window_event_t event;
        sys_yield();
        while (sys_window_poll(files_window_handle, &event) == 1) {
            if (event.type == USER_WINDOW_EVENT_CLOSE) {
                files_close_requested = 1;
                break;
            }
            if (event.type == USER_WINDOW_EVENT_REDRAW) {
                files_window.x = event.x;
                files_window.y = event.y;
            } else if (event.type == USER_WINDOW_EVENT_KEY) {
                files_handle_key(&files_window, (char)event.key);
            } else if (event.type == USER_WINDOW_EVENT_MOUSE) {
                files_handle_mouse(&files_window, event.x, event.y,
                                  (uint8_t)event.buttons);
            }
            if (files_close_requested) break;
            files_draw_content(&files_window);
            os->graphics.blit_all();
        }
    }
    os->windows.unregister_window(&files_window);
}

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


static char current_dir[FS_MAX_PATH] = "/";
static uint8_t file_types[FS_MAX_FILES];
static uint32_t picker_token;
static int picker_finished;
static int first_row;
static char status[64];
static int last_clicked = -1;
static uint32_t last_click_tick;
#define ROW_TOP 102
#define ROW_HEIGHT 18
#define VISIBLE_ROWS 13

static void copy_text(char* out, const char* in, unsigned capacity) {
    unsigned i = 0;
    while (in[i] && i+1 < capacity) { out[i] = in[i]; i++; }
    out[i] = 0;
}
static int suffix(const char* path, const char* extension) {
    int n = 0; while (path[n]) n++;
    if (n < 4) return 0;
    for (int i = 0; i < 4; i++) {
        char c = path[n-4+i]; if (c >= 'a' && c <= 'z') c -= 32;
        if (c != extension[i]) return 0;
    }
    return 1;
}
static int item_path(int i, char* out) {
    copy_text(out, current_dir, FS_MAX_PATH);
    unsigned n = 0, size = 0; while (out[n]) n++;
    while (file_list[i][size]) size++;
    if (n+size+1 >= FS_MAX_PATH) return 0;
    if (n > 1) out[n++] = '/';
    copy_text(out+n, file_list[i], FS_MAX_PATH-n);
    return 1;
}
static void clamp_scroll(void) {
    int maximum = file_count > VISIBLE_ROWS ? file_count - VISIBLE_ROWS : 0;
    if (first_row < 0) first_row = 0;
    if (first_row > maximum) first_row = maximum;
}
static void show_selection(void) {
    if (selected_file < first_row) first_row = selected_file;
    if (selected_file >= first_row+VISIBLE_ROWS) first_row = selected_file-VISIBLE_ROWS+1;
    clamp_scroll();
}
static int load_directory(const char* path) {
    /* Use scratch output so a rejected navigation preserves the current view. */
    char names[FS_MAX_FILES][FS_MAX_FILENAME]; uint8_t types[FS_MAX_FILES];
    int count = sys_fs_list_dir(path, names, types);
    if (count < 0) { copy_text(status, "Cannot open directory: missing or permission denied.", sizeof(status)); return 0; }
    copy_text(current_dir, path, sizeof(current_dir));
    file_count = 0;
    for (int i = 0; i < count; i++) {
        if (picker_token && !(types[i] & FAT_ATTR_DIRECTORY) && !suffix(names[i], ".BIN")) continue;
        copy_text(file_list[file_count], names[i], FS_MAX_FILENAME);
        file_types[file_count] = types[i]; file_sizes[file_count++] = -2;
    }
    count = file_count;
    selected_file = count ? 0 : -1; first_row = 0; last_clicked = -1; status[0] = 0;
    return 1;
}
static void parent_directory(void) {
    char path[FS_MAX_PATH]; copy_text(path, current_dir, sizeof(path));
    int n = 0; while (path[n]) n++;
    while (n > 1 && path[n-1] != '/') n--;
    if (n > 1) n--;
    path[n] = 0;
    load_directory(path);
}
static void launch(const char* path) {
    if (sys_app_launch(path, "") < 0)
        copy_text(status, "Cannot launch application: check access or free slots.", sizeof(status));
    else copy_text(status, "Application launched.", sizeof(status));
}
static void open_selected(void) {
    if (selected_file < 0 || selected_file >= file_count) return;
    char path[FS_MAX_PATH];
    if (!item_path(selected_file, path)) { copy_text(status, "Path too long.", sizeof(status)); return; }
    if (file_types[selected_file] & FAT_ATTR_DIRECTORY) load_directory(path);
    else if (picker_token) {
        if (sys_file_pick_result(picker_token, path) == 0) {
            picker_finished = 1; files_close_requested = 1;
        } else copy_text(status, "Selection request expired or program inaccessible.", sizeof(status));
    }
    else if (suffix(path, ".WAV") || suffix(path, ".MP3")) {
        if (sys_app_launch("/sound.bin", path) < 0)
            copy_text(status, "Cannot launch Sound Player.", sizeof(status));
        else copy_text(status, "Opened in Sound Player.", sizeof(status));
    }
    else if (suffix(path, ".BIN")) launch(path);
    else if (suffix(path, ".LNK")) {
        struct { uint32_t magic; int32_t x,y; uint32_t icon; char target[FS_MAX_PATH]; char label[16]; } link;
        if (sys_fs_read(path, &link, sizeof(link)) == sizeof(link) && link.magic == 0x314B5344 &&
            !link.target[FS_MAX_PATH-1] && suffix(link.target, ".BIN")) launch(link.target);
        else copy_text(status, "Invalid shortcut.", sizeof(status));
    } else copy_text(status, "Select a folder, application, shortcut, or audio file.", sizeof(status));
}
static void itoa(int value, char* out) {
    char reverse[12]; int n = 0, i = 0;
    do { reverse[n++] = '0'+value%10; value /= 10; } while (value);
    while (n) out[i++] = reverse[--n]; out[i] = 0;
}
static void label(int x, int y, const char* value, uint32_t color) {
    user_draw_string(files_window.x+x, files_window.y+y, value, color);
}
static void rect(int x, int y, int w, int h, uint32_t color) {
    user_fill_rect(files_window.x+x, files_window.y+y, w, h, color);
}
/* Match the shared raised buttons and the sunken panels used by Calculator
 * and Notepad rather than drawing flat outlines. */
static void raised_button(int x, int y, int w, int h, const char* value) {
    user_window_draw_t drawing = {0};
    drawing.operation = USER_WINDOW_DRAW_BUTTON;
    drawing.x = x; drawing.y = y; drawing.width = w; drawing.height = h;
    drawing.color = COLOR_WIN_BG;
    drawing.text = w < 20 ? "" : value;
    (void)sys_window_draw(files_window_handle, &drawing);
    if (w < 20 && value[0]) label(x+5, y+(h/2)-4, value, COLOR_BLACK);
}
static void inset(int x, int y, int w, int h, uint32_t fill) {
    rect(x, y, w, h, fill);
    rect(x-1, y-1, w+2, 1, COLOR_DARK_GRAY);
    rect(x-1, y-1, 1, h+2, COLOR_DARK_GRAY);
    rect(x-1, y+h, w+2, 1, COLOR_WHITE);
    rect(x+w, y-1, 1, h+2, COLOR_WHITE);
}
static void button(int x, int w, const char* value) {
    raised_button(x, 28, w, 22, value);
}
void files_draw_content(Window* win) {
    (void)win;
    rect(3, 21, 514, 366, COLOR_WIN_BG);
    button(10, 56, "Up"); button(76, 80, "Refresh"); button(166, 64, "Open");
    char path[61]; unsigned length = 0; while (current_dir[length]) length++;
    copy_text(path, current_dir+(length > 60 ? length-60 : 0), sizeof(path));
    inset(10, 57, 500, 17, COLOR_WHITE);
    label(14, 62, path, COLOR_BLACK);
    inset(8, 78, 480, 260, COLOR_WHITE);
    raised_button(10, 80, 284, 20, "Name");
    raised_button(294, 80, 192, 20, "Type / size");
    for (int row = 0; row < VISIBLE_ROWS && first_row+row < file_count; row++) {
        int i = first_row+row, y = ROW_TOP+row*ROW_HEIGHT;
        uint32_t color = i == selected_file ? COLOR_WHITE : COLOR_BLACK;
        if (i == selected_file) rect(10, y-1, 476, ROW_HEIGHT, COLOR_SELECT_BG);
        label(14, y+3, file_list[i], color);
        if (file_types[i] & FAT_ATTR_DIRECTORY) label(300, y+3, "Folder", color);
        else {
            if (file_sizes[i] == -2) {
                char item[FS_MAX_PATH]; file_sizes[i] = item_path(i, item) ? sys_fs_read(item, 0, 0) : -1;
            }
            char size[16];
            if (file_sizes[i] < 0) copy_text(size, "Unavailable", sizeof(size));
            else { itoa(file_sizes[i], size); int n = 0; while (size[n]) n++; copy_text(size+n, " B", sizeof(size)-n); }
            label(300, y+3, size, color);
        }
    }
    if (!file_count) label(14, ROW_TOP+4, "This directory is empty.", COLOR_DARK_GRAY);
    int bottom = ROW_TOP+VISIBLE_ROWS*ROW_HEIGHT;
    inset(494, ROW_TOP, 18, bottom-ROW_TOP, 0x00DEDEDE);
    raised_button(494, ROW_TOP, 18, 18, "^");
    raised_button(494, bottom-18, 18, 18, "v");
    int track = bottom-ROW_TOP-36;
    int thumb = file_count > VISIBLE_ROWS ? track*VISIBLE_ROWS/file_count : track;
    if (thumb < 12) thumb = 12;
    int offset = file_count > VISIBLE_ROWS ? (track-thumb)*first_row/(file_count-VISIBLE_ROWS) : 0;
    raised_button(495, ROW_TOP+18+offset, 16, thumb, "");
    inset(10, 346, 88, 16, COLOR_WIN_BG);
    inset(10, 365, 500, 18, COLOR_WIN_BG);
    char count[16]; itoa(file_count, count); label(12, 350, count, COLOR_BLACK);
    label(52, 350, "items", COLOR_BLACK);
    label(12, 369, status[0] ? status : "Enter / double-click: open   Backspace: up   F5: refresh", COLOR_BLACK);
}
void files_handle_key(Window* win, char key) {
    (void)win; uint8_t c = key;
    if (c == 0x50 && selected_file+1 < file_count) selected_file++;
    else if (c == 0x48 && selected_file > 0) selected_file--;
    else if (c == 0x49) { selected_file -= VISIBLE_ROWS; if (selected_file < 0) selected_file = file_count ? 0 : -1; }
    else if (c == 0x51) { selected_file += VISIBLE_ROWS; if (selected_file >= file_count) selected_file = file_count-1; }
    else if (c == 0x47) selected_file = file_count ? 0 : -1;
    else if (c == 0x4F) selected_file = file_count-1;
    else if (c == '\n') open_selected();
    else if (c == '\b') parent_directory();
    else if (c == 0x3F) load_directory(current_dir);
    else if (c == 1) files_close_requested = 1;
    show_selection();
}
void files_handle_mouse(Window* win, int32_t x, int32_t y, uint8_t buttons) {
    (void)win;
    if (!(buttons & 1)) return;
    if (y >= 28 && y < 50) {
        if (x >= 10 && x < 66) parent_directory();
        else if (x >= 76 && x < 156) load_directory(current_dir);
        else if (x >= 166 && x < 230) open_selected();
        return;
    }
    int bottom = ROW_TOP+VISIBLE_ROWS*ROW_HEIGHT;
    if (x >= 494 && x < 512 && y >= ROW_TOP && y < bottom) {
        if (y < ROW_TOP+18) first_row--;
        else if (y >= bottom-18) first_row++;
        else if (file_count > VISIBLE_ROWS) first_row = (y-ROW_TOP-18)*(file_count-VISIBLE_ROWS)/(bottom-ROW_TOP-36);
        clamp_scroll(); last_clicked = -1; return;
    }
    if (x < 10 || x >= 488 || y < ROW_TOP || y >= bottom) return;
    int index = first_row+(y-ROW_TOP)/ROW_HEIGHT;
    if (index >= file_count) return;
    selected_file = index;
    uint32_t now = sys_get_ticks();
    if (last_clicked == index && now-last_click_tick <= 50) { last_clicked = -1; open_selected(); }
    else { last_clicked = index; last_click_tick = now; }
}
void user_main(const char* arguments) {
    files_window_handle = 0; files_close_requested = 0;
    user_api.graphics.fill_rect = user_fill_rect;
    user_api.graphics.draw_string = user_draw_string;
    user_api.graphics.blit_all = user_blit_all;
    user_api.windows.register_window = user_register_window;
    user_api.windows.unregister_window = user_unregister_window;
    files_window.x = 150; files_window.y = 80; files_window.w = 520; files_window.h = 390;
    picker_token = 0; picker_finished = 0;
    const char* prefix = "--pick-bin "; int match = 0;
    while (arguments && prefix[match] && arguments[match] == prefix[match]) match++;
    if (!prefix[match]) {
        const char* number = arguments+match;
        while (*number >= '0' && *number <= '9') picker_token = picker_token*10 + (*number++-'0');
        arguments = "/";
    }
    files_window.title = picker_token ? "Select a program" : "Files"; files_window.visible = 1; files_window.focused = 1;
    files_window.draw_content = files_draw_content;
    files_window.handle_key = files_handle_key; files_window.handle_mouse = files_handle_mouse;
    /* A single directory argument, absolute or relative to the filesystem root. */
    char start[FS_MAX_PATH] = "/";
    while (arguments && *arguments == ' ') arguments++;
    if (arguments && *arguments) {
        if (*arguments == '/') copy_text(start, arguments, sizeof(start));
        else copy_text(start+1, arguments, sizeof(start)-1);
        int n = 0; while (start[n]) n++;
        while (n > 1 && (start[n-1] == '/' || start[n-1] == ' ')) start[--n] = 0;
    }
    if (!load_directory(start)) {
        load_directory("/"); copy_text(status, "Cannot open starting directory; showing root.", sizeof(status));
    }
    if (user_register_window(&files_window) < 0) { sys_write("files: no window slots\n", 23); return; }
    files_draw_content(&files_window); user_blit_all();
    while (!files_close_requested) {
        user_window_event_t event; sys_yield();
        while (sys_window_poll(files_window_handle, &event) == 1) {
            if (event.type == USER_WINDOW_EVENT_CLOSE) { files_close_requested = 1; break; }
            if (event.type == USER_WINDOW_EVENT_REDRAW) { files_window.x = event.x; files_window.y = event.y; }
            else if (event.type == USER_WINDOW_EVENT_KEY) files_handle_key(&files_window, event.key);
            else if (event.type == USER_WINDOW_EVENT_MOUSE) files_handle_mouse(&files_window, event.x, event.y, event.buttons);
            if (!files_close_requested) { files_draw_content(&files_window); user_blit_all(); }
        }
    }
    if (picker_token && !picker_finished) (void)sys_file_pick_result(picker_token, "");
    user_unregister_window(&files_window);
}

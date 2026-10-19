#include "wm.h"

static Window* windows[WM_MAX_WINDOWS];
static void (*invalidate)(void);
static os_api_t* wm_api;

void wm_initialize(os_api_t* api, void (*invalidate_callback)(void)) {
    wm_api = api;
    invalidate = invalidate_callback;
}

static void draw_window_frame(Window* window) {
    if (!wm_api || !window || !window->visible) return;
    wm_api->fill_rect(window->x, window->y, window->w, window->h, COLOR_WIN_BG);
    wm_api->fill_rect(window->x + 2, window->y + 2, window->w - 4, 15,
                      window->focused ? COLOR_TITLE_BAR : COLOR_DARK_GRAY);
    if (window->title)
        wm_api->draw_string(window->x + 6, window->y + 5, window->title, COLOR_WHITE);

    int32_t close_x = window->x + window->w - 16;
    int32_t close_y = window->y + 3;
    wm_api->draw_button(close_x, close_y, 13, 13, 0, 0);
    wm_api->draw_string(close_x + 3, close_y + 2, "x", COLOR_BLACK);
    wm_api->fill_rect(window->x, window->y, window->w, 1, COLOR_WHITE);
    wm_api->fill_rect(window->x, window->y, 1, window->h, COLOR_WHITE);
    wm_api->fill_rect(window->x, window->y + window->h - 1, window->w, 1, COLOR_DARK_GRAY);
    wm_api->fill_rect(window->x + window->w - 1, window->y, 1, window->h, COLOR_DARK_GRAY);
    if (window->draw_content) window->draw_content(window);
}

void wm_draw_windows(void) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++) draw_window_frame(windows[i]);
}

static Window* find_window(const char* title) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        Window* window = windows[i];
        if (!window || !window->title) continue;
        const char* left = window->title;
        const char* right = title;
        while (*left && *right && *left == *right) { left++; right++; }
        if (!*left && !*right) return window;
    }
    return 0;
}

void wm_draw_taskbar(uint8_t start_menu_open) {
    if (!wm_api) return;
    const int y = WM_TASKBAR_Y;
    wm_api->fill_rect(0, y, SCREEN_WIDTH, WM_TASKBAR_HEIGHT, COLOR_WIN_BG);
    wm_api->fill_rect(0, y, SCREEN_WIDTH, 1, COLOR_WHITE);
    wm_api->draw_button(4, y + 4, 60, 24, "Start", start_menu_open);

    Window* terminal = find_window("Terminal");
    Window* notepad = find_window("Notepad");
    Window* viewer = find_window("Viewer");
    Window* files = find_window("Files");
    wm_api->draw_button(68, y + 4, 80, 24, "Terminal", terminal && terminal->visible && terminal->focused);
    wm_api->draw_button(152, y + 4, 80, 24, "Notepad", notepad && notepad->visible && notepad->focused);
    wm_api->draw_button(236, y + 4, 80, 24, "Viewer", viewer && viewer->visible && viewer->focused);
    if (files) wm_api->draw_button(320, y + 4, 80, 24, "Files", files->visible && files->focused);

    wm_api->fill_rect(SCREEN_WIDTH - 64, y + 5, 58, 22, COLOR_WIN_BG);
    wm_api->fill_rect(SCREEN_WIDTH - 64, y + 5, 58, 1, COLOR_DARK_GRAY);
    wm_api->fill_rect(SCREEN_WIDTH - 64, y + 5, 1, 22, COLOR_DARK_GRAY);
    wm_api->fill_rect(SCREEN_WIDTH - 64, y + 26, 58, 1, COLOR_WHITE);
    wm_api->fill_rect(SCREEN_WIDTH - 7, y + 5, 1, 22, COLOR_WHITE);
    wm_api->draw_string(SCREEN_WIDTH - 52, y + 11, "12:00", COLOR_BLACK);

    if (!start_menu_open) return;
    int menu_x = 4, menu_y = y - 96, menu_w = 120, menu_h = 92;
    wm_api->fill_rect(menu_x, menu_y, menu_w, menu_h, COLOR_WIN_BG);
    wm_api->fill_rect(menu_x, menu_y, menu_w, 1, COLOR_WHITE);
    wm_api->fill_rect(menu_x, menu_y, 1, menu_h, COLOR_WHITE);
    wm_api->fill_rect(menu_x, menu_y + menu_h - 1, menu_w, 1, COLOR_DARK_GRAY);
    wm_api->fill_rect(menu_x + menu_w - 1, menu_y, 1, menu_h, COLOR_DARK_GRAY);
    wm_api->draw_string(menu_x + 12, menu_y + 10, "Terminal", COLOR_BLACK);
    wm_api->fill_rect(menu_x + 6, menu_y + 28, menu_w - 12, 1, COLOR_DARK_GRAY);
    wm_api->fill_rect(menu_x + 6, menu_y + 29, menu_w - 12, 1, COLOR_WHITE);
    wm_api->draw_string(menu_x + 12, menu_y + 38, "Notepad", COLOR_BLACK);
    wm_api->fill_rect(menu_x + 6, menu_y + 56, menu_w - 12, 1, COLOR_DARK_GRAY);
    wm_api->fill_rect(menu_x + 6, menu_y + 57, menu_w - 12, 1, COLOR_WHITE);
    wm_api->draw_string(menu_x + 12, menu_y + 66, "Viewer", COLOR_BLACK);
}

Window* wm_get_window(int index) {
    if (index < 0 || index >= WM_MAX_WINDOWS) return 0;
    return windows[index];
}

void wm_bring_to_front(Window* target) {
    if (!target) return;

    int target_index = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (windows[i]) windows[i]->focused = (windows[i] == target);
        if (windows[i] == target) target_index = i;
    }
    if (target_index < 0) return;

    int last = WM_MAX_WINDOWS - 1;
    while (last > target_index && windows[last] == 0) last--;
    for (int i = target_index; i < last; i++) windows[i] = windows[i + 1];
    windows[last] = target;
}

int wm_register_window(Window* window) {
    if (!window) return -1;

    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (windows[i] == window) {
            window->visible = 1;
            window->focused = 1;
            wm_bring_to_front(window);
            if (invalidate) invalidate();
            return i;
        }
    }

    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!windows[i]) {
            windows[i] = window;
            window->visible = 1;
            window->focused = 1;
            for (int j = 0; j < WM_MAX_WINDOWS; j++) {
                if (windows[j] && windows[j] != window) windows[j]->focused = 0;
            }
            wm_bring_to_front(window);
            if (invalidate) invalidate();
            return i;
        }
    }
    return -1;
}

void wm_unregister_window(Window* window) {
    if (!window) return;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (windows[i] != window) continue;
        for (int j = i; j < WM_MAX_WINDOWS - 1; j++) windows[j] = windows[j + 1];
        windows[WM_MAX_WINDOWS - 1] = 0;
        if (invalidate) invalidate();
        return;
    }
}

#include "wm.h"
#include "desktop.h"
#include "../../../kernel/paging.h"
#include "../../loader/app_loader.h"

static Window* windows[WM_MAX_WINDOWS];
static uint32_t window_directories[WM_MAX_WINDOWS];
static void (*invalidate)(void);
static os_api_t* wm_api;
static int32_t mouse_x = 400;
static int32_t mouse_y = 300;
static Window* dragging_window;
static uint32_t dragging_directory;
static uint8_t previous_left_button;
static uint8_t start_menu_open;
static uint8_t redraw_pending;

static void request_redraw(void) {
    redraw_pending = 1;
    if (invalidate) invalidate();
}

static uint32_t window_context_enter(int index) {
    uint32_t previous = paging_current_directory();
    uint32_t desired = window_directories[index];
    if (!desired) desired = paging_kernel_directory();
    if (previous != desired) paging_activate_directory(desired);
    return previous;
}

static void window_context_leave(uint32_t previous) {
    if (paging_current_directory() != previous) paging_activate_directory(previous);
    app_loader_collect();
}

static int find_window_index(const char* title) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!windows[i]) continue;
        uint32_t previous = window_context_enter(i);
        const char* left = windows[i]->title;
        const char* right = title;
        while (left && right && *left && *right && *left == *right) {
            left++;
            right++;
        }
        int matches = left && right && !*left && !*right;
        window_context_leave(previous);
        if (matches) return i;
    }
    return -1;
}

static uint8_t window_state(int index) {
    if (index < 0 || index >= WM_MAX_WINDOWS || !windows[index]) return 0;
    uint32_t previous = window_context_enter(index);
    uint8_t state = (windows[index]->visible ? 1 : 0) |
                    (windows[index]->focused ? 2 : 0);
    window_context_leave(previous);
    return state;
}

static void wm_bring_to_front_index(int target_index) {
    if (target_index < 0 || target_index >= WM_MAX_WINDOWS || !windows[target_index]) return;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!windows[i]) continue;
        uint32_t previous = window_context_enter(i);
        windows[i]->focused = (i == target_index);
        window_context_leave(previous);
    }
    int last = WM_MAX_WINDOWS - 1;
    while (last > target_index && windows[last] == 0) last--;
    Window* target = windows[target_index];
    uint32_t target_directory = window_directories[target_index];
    for (int i = target_index; i < last; i++) {
        windows[i] = windows[i + 1];
        window_directories[i] = window_directories[i + 1];
    }
    windows[last] = target;
    window_directories[last] = target_directory;
}

void wm_initialize(os_api_t* api, void (*invalidate_callback)(void)) {
    wm_api = api;
    invalidate = invalidate_callback;
}

static void draw_window_frame(Window* window) {
    if (!wm_api || !window || !window->visible) return;
    wm_api->graphics.fill_rect(window->x, window->y, window->w, window->h, COLOR_WIN_BG);
    wm_api->graphics.fill_rect(window->x + 2, window->y + 2, window->w - 4, 15,
                      window->focused ? COLOR_TITLE_BAR : COLOR_DARK_GRAY);
    if (window->title)
        wm_api->graphics.draw_string(window->x + 6, window->y + 5, window->title, COLOR_WHITE);

    int32_t close_x = window->x + window->w - 16;
    int32_t close_y = window->y + 3;
    wm_api->graphics.draw_button(close_x, close_y, 13, 13, 0, 0);
    wm_api->graphics.draw_string(close_x + 3, close_y + 2, "x", COLOR_BLACK);
    wm_api->graphics.fill_rect(window->x, window->y, window->w, 1, COLOR_WHITE);
    wm_api->graphics.fill_rect(window->x, window->y, 1, window->h, COLOR_WHITE);
    wm_api->graphics.fill_rect(window->x, window->y + window->h - 1, window->w, 1, COLOR_DARK_GRAY);
    wm_api->graphics.fill_rect(window->x + window->w - 1, window->y, 1, window->h, COLOR_DARK_GRAY);
    if (window->draw_content) window->draw_content(window);
}

void wm_draw_windows(void) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!windows[i]) continue;
        uint32_t previous = window_context_enter(i);
        draw_window_frame(windows[i]);
        window_context_leave(previous);
    }
}

void wm_draw_taskbar(uint8_t start_menu_open) {
    if (!wm_api) return;
    const int y = WM_TASKBAR_Y;
    wm_api->graphics.fill_rect(0, y, SCREEN_WIDTH, WM_TASKBAR_HEIGHT, COLOR_WIN_BG);
    wm_api->graphics.fill_rect(0, y, SCREEN_WIDTH, 1, COLOR_WHITE);
    wm_api->graphics.draw_button(4, y + 4, 60, 24, "Start", start_menu_open);

    int terminal = find_window_index("Terminal");
    int notepad = find_window_index("Notepad");
    int viewer = find_window_index("Viewer");
    int files = find_window_index("Files");
    uint8_t terminal_state = window_state(terminal);
    uint8_t notepad_state = window_state(notepad);
    uint8_t viewer_state = window_state(viewer);
    uint8_t files_state = window_state(files);
    wm_api->graphics.draw_button(68, y + 4, 80, 24, "Terminal", (terminal_state & 3) == 3);
    wm_api->graphics.draw_button(152, y + 4, 80, 24, "Notepad", (notepad_state & 3) == 3);
    wm_api->graphics.draw_button(236, y + 4, 80, 24, "Viewer", (viewer_state & 3) == 3);
    if (files >= 0) wm_api->graphics.draw_button(320, y + 4, 80, 24, "Files", (files_state & 3) == 3);

    wm_api->graphics.fill_rect(SCREEN_WIDTH - 64, y + 5, 58, 22, COLOR_WIN_BG);
    wm_api->graphics.fill_rect(SCREEN_WIDTH - 64, y + 5, 58, 1, COLOR_DARK_GRAY);
    wm_api->graphics.fill_rect(SCREEN_WIDTH - 64, y + 5, 1, 22, COLOR_DARK_GRAY);
    wm_api->graphics.fill_rect(SCREEN_WIDTH - 64, y + 26, 58, 1, COLOR_WHITE);
    wm_api->graphics.fill_rect(SCREEN_WIDTH - 7, y + 5, 1, 22, COLOR_WHITE);
    wm_api->graphics.draw_string(SCREEN_WIDTH - 52, y + 11, "12:00", COLOR_BLACK);

    if (!start_menu_open) return;
    int menu_x = 4, menu_y = y - 96, menu_w = 120, menu_h = 92;
    wm_api->graphics.fill_rect(menu_x, menu_y, menu_w, menu_h, COLOR_WIN_BG);
    wm_api->graphics.fill_rect(menu_x, menu_y, menu_w, 1, COLOR_WHITE);
    wm_api->graphics.fill_rect(menu_x, menu_y, 1, menu_h, COLOR_WHITE);
    wm_api->graphics.fill_rect(menu_x, menu_y + menu_h - 1, menu_w, 1, COLOR_DARK_GRAY);
    wm_api->graphics.fill_rect(menu_x + menu_w - 1, menu_y, 1, menu_h, COLOR_DARK_GRAY);
    wm_api->graphics.draw_string(menu_x + 12, menu_y + 10, "Terminal", COLOR_BLACK);
    wm_api->graphics.fill_rect(menu_x + 6, menu_y + 28, menu_w - 12, 1, COLOR_DARK_GRAY);
    wm_api->graphics.fill_rect(menu_x + 6, menu_y + 29, menu_w - 12, 1, COLOR_WHITE);
    wm_api->graphics.draw_string(menu_x + 12, menu_y + 38, "Notepad", COLOR_BLACK);
    wm_api->graphics.fill_rect(menu_x + 6, menu_y + 56, menu_w - 12, 1, COLOR_DARK_GRAY);
    wm_api->graphics.fill_rect(menu_x + 6, menu_y + 57, menu_w - 12, 1, COLOR_WHITE);
    wm_api->graphics.draw_string(menu_x + 12, menu_y + 66, "Viewer", COLOR_BLACK);
}

Window* wm_get_window(int index) {
    if (index < 0 || index >= WM_MAX_WINDOWS) return 0;
    return windows[index];
}

void wm_bring_to_front(Window* target) {
    if (!target) return;
    uint32_t directory = paging_current_directory();
    if (directory == paging_kernel_directory()) directory = 0;
    int target_index = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (windows[i] == target && window_directories[i] == directory) {
            target_index = i;
            break;
        }
    }
    wm_bring_to_front_index(target_index);
}

int wm_register_window(Window* window) {
    if (!window) return -1;
    uint32_t owner_directory = paging_current_directory();
    if (owner_directory == paging_kernel_directory()) owner_directory = 0;

    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (windows[i] == window && window_directories[i] == owner_directory) {
            uint32_t previous = window_context_enter(i);
            window->visible = 1;
            window->focused = 1;
            window_context_leave(previous);
            wm_bring_to_front_index(i);
            request_redraw();
            return i;
        }
    }

    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!windows[i]) {
            windows[i] = window;
            window_directories[i] = owner_directory;
            uint32_t previous = window_context_enter(i);
            window->visible = 1;
            window->focused = 1;
            window_context_leave(previous);
            for (int j = 0; j < WM_MAX_WINDOWS; j++) {
                if (j == i || !windows[j]) continue;
                previous = window_context_enter(j);
                windows[j]->focused = 0;
                window_context_leave(previous);
            }
            if (owner_directory) app_loader_window_attached(owner_directory);
            wm_bring_to_front_index(i);
            request_redraw();
            return i;
        }
    }
    return -1;
}

void wm_unregister_window(Window* window) {
    if (!window) return;
    uint32_t owner_directory = paging_current_directory();
    if (owner_directory == paging_kernel_directory()) owner_directory = 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (windows[i] != window || window_directories[i] != owner_directory) continue;
        for (int j = i; j < WM_MAX_WINDOWS - 1; j++) {
            windows[j] = windows[j + 1];
            window_directories[j] = window_directories[j + 1];
        }
        windows[WM_MAX_WINDOWS - 1] = 0;
        window_directories[WM_MAX_WINDOWS - 1] = 0;
        if (owner_directory) app_loader_window_detached(owner_directory);
        request_redraw();
        return;
    }
}

static void show_window(const char* title) {
    int index = find_window_index(title);
    if (index < 0) return;
    uint32_t previous = window_context_enter(index);
    windows[index]->visible = 1;
    window_context_leave(previous);
    wm_bring_to_front_index(index);
    request_redraw();
}

static void toggle_window(const char* title) {
    int index = find_window_index(title);
    if (index < 0) return;
    uint32_t previous = window_context_enter(index);
    windows[index]->visible = !windows[index]->visible;
    uint8_t visible = windows[index]->visible;
    window_context_leave(previous);
    if (visible) wm_bring_to_front_index(index);
    start_menu_open = 0;
    request_redraw();
}

static void handle_mouse_event(const os_input_event_t* event) {
    mouse_x += event->mouse_dx;
    mouse_y -= event->mouse_dy;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x > SCREEN_WIDTH - 12) mouse_x = SCREEN_WIDTH - 12;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y > SCREEN_HEIGHT - 19) mouse_y = SCREEN_HEIGHT - 19;

    uint8_t left = event->mouse_buttons & 1;
    uint8_t click_down = left && !previous_left_button;
    if (event->mouse_dx || event->mouse_dy) {
        if (dragging_window && left) {
            for (int i = 0; i < WM_MAX_WINDOWS; i++) {
                if (windows[i] != dragging_window ||
                    window_directories[i] != dragging_directory) continue;
                uint32_t previous = window_context_enter(i);
                windows[i]->x += event->mouse_dx;
                windows[i]->y -= event->mouse_dy;
                if (windows[i]->y < 0) windows[i]->y = 0;
                if (windows[i]->y > WM_TASKBAR_Y - 24)
                    windows[i]->y = WM_TASKBAR_Y - 24;
                window_context_leave(previous);
                request_redraw();
                break;
            }
        }
        if (wm_api->input.move_cursor)
            wm_api->input.move_cursor(mouse_x, mouse_y, dragging_window && left ? 0 : 1);
    }
    if (left != previous_left_button) request_redraw();

    if (!left) {
        dragging_window = 0;
        dragging_directory = 0;
        previous_left_button = left;
        return;
    }

    if (click_down) {
        int handled = 0;
        if (mouse_x >= 4 && mouse_x <= 64 && mouse_y >= WM_TASKBAR_Y + 4 && mouse_y <= WM_TASKBAR_Y + 28) {
            start_menu_open = !start_menu_open;
            handled = 1;
            request_redraw();
        } else if (mouse_y >= WM_TASKBAR_Y + 4 && mouse_y <= WM_TASKBAR_Y + 28) {
            if (mouse_x >= 68 && mouse_x <= 148) { toggle_window("Terminal"); handled = 1; }
            else if (mouse_x >= 152 && mouse_x <= 232) { toggle_window("Notepad"); handled = 1; }
            else if (mouse_x >= 236 && mouse_x <= 316) { toggle_window("Viewer"); handled = 1; }
            else if (mouse_x >= 320 && mouse_x <= 400) { toggle_window("Files"); handled = 1; }
        } else if (start_menu_open && mouse_x >= 4 && mouse_x <= 124 &&
                   mouse_y >= WM_TASKBAR_Y - 96 && mouse_y < WM_TASKBAR_Y) {
            if (mouse_y < WM_TASKBAR_Y - 64) show_window("Terminal");
            else if (mouse_y < WM_TASKBAR_Y - 32) show_window("Notepad");
            else show_window("Viewer");
            start_menu_open = 0;
            handled = 1;
        }

        if (!handled) {
            start_menu_open = 0;
            for (int i = WM_MAX_WINDOWS - 1; i >= 0; i--) {
                Window* window = windows[i];
                if (!window) continue;
                uint32_t previous = window_context_enter(i);
                if (!window->visible) {
                    window_context_leave(previous);
                    continue;
                }
                int close_x = window->x + window->w - 16;
                int close_y = window->y + 3;
                if (mouse_x >= close_x && mouse_x <= close_x + 13 &&
                    mouse_y >= close_y && mouse_y <= close_y + 13) {
                    uint32_t owner_directory = window_directories[i];
                    if (owner_directory) {
                        wm_unregister_window(window);
                        dragging_window = 0;
                        dragging_directory = 0;
                    } else {
                        window->visible = 0;
                    }
                    window_context_leave(previous);
                    request_redraw();
                    handled = 1;
                    break;
                }
                if (mouse_x >= window->x && mouse_x <= window->x + window->w &&
                    mouse_y >= window->y && mouse_y <= window->y + 18) {
                    uint32_t owner_directory = window_directories[i];
                    window_context_leave(previous);
                    if (owner_directory) paging_activate_directory(owner_directory);
                    wm_bring_to_front(window);
                    if (owner_directory) paging_activate_kernel();
                    dragging_window = window;
                    dragging_directory = owner_directory;
                    request_redraw();
                    handled = 1;
                    break;
                }
                if (mouse_x >= window->x && mouse_x <= window->x + window->w &&
                    mouse_y >= window->y && mouse_y <= window->y + window->h) {
                    uint32_t owner_directory = window_directories[i];
                    window_context_leave(previous);
                    if (owner_directory) paging_activate_directory(owner_directory);
                    wm_bring_to_front(window);
                    if (owner_directory) paging_activate_kernel();
                    request_redraw();
                    handled = 1;
                    previous = paging_current_directory();
                    if (owner_directory) paging_activate_directory(owner_directory);
                    if (window->handle_mouse)
                        window->handle_mouse(window, mouse_x - window->x,
                                             mouse_y - window->y, left);
                    window_context_leave(previous);
                    break;
                }
                window_context_leave(previous);
            }
        }
        if (!handled && mouse_y < WM_TASKBAR_Y && wm_api->network.get_ticks &&
            desktop_handle_click(mouse_x, mouse_y, wm_api->network.get_ticks()))
            request_redraw();
    }
    previous_left_button = left;
}

static void handle_key_event(const os_input_event_t* event) {
    uint8_t scancode = event->scancode;
    if (scancode == 0x58) {
        if (wm_api->graphics.capture_screenshot) wm_api->graphics.capture_screenshot();
        request_redraw();
    }

    char key = (scancode == 0x3C || scancode == 0x3D || scancode == 0x48 ||
                scancode == 0x50 || scancode == 0x01) ? (char)scancode :
                (wm_api->input.translate_scancode ? wm_api->input.translate_scancode(scancode) : 0);
    if (!key) return;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        Window* window = windows[i];
        if (!window) continue;
        uint32_t previous = window_context_enter(i);
        if (window->visible && window->focused && window->handle_key) {
            window->handle_key(window, key);
            window_context_leave(previous);
            request_redraw();
            return;
        }
        window_context_leave(previous);
    }
}

uint8_t wm_start_menu_is_open(void) {
    return start_menu_open;
}

void wm_poll(void) {
    if (!wm_api) return;
    os_input_event_t event;
    event.type = OS_INPUT_NONE;
    if (wm_api->input.poll_input && wm_api->input.poll_input(&event)) {
        if (event.type == OS_INPUT_MOUSE) handle_mouse_event(&event);
        else if (event.type == OS_INPUT_KEY) handle_key_event(&event);
    }
    if (wm_api->graphics.take_redraw_request && wm_api->graphics.take_redraw_request())
        redraw_pending = 1;
    if (redraw_pending && wm_api->graphics.render_screen) {
        wm_api->graphics.render_screen();
        redraw_pending = 0;
    }
}

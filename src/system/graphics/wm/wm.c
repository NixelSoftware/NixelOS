#include "wm.h"

static Window* windows[WM_MAX_WINDOWS];
static void (*invalidate)(void);

void wm_initialize(void (*invalidate_callback)(void)) {
    invalidate = invalidate_callback;
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

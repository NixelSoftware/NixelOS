#include "../../services/audio/mixer.h"
#include "../settings/settings.h"
#include "../../../kernel/hal/audio.h"
#include "../../../kernel/hal/video.h"
#include "../../../kernel/mutex.h"
#include "wm.h"
#include "start.h"
#include "desktop.h"
#include "desktop_wallpaper.h"
#include "cursor.h"
#include "theme.h"
#include "../../../kernel/paging.h"
#include "../../../kernel/drivers/display.h"
#include "../../loader/app_loader.h"
#include "../widgets.h"
#include "../../../kernel/drivers/display/font.h"
#include "../../../kernel/drivers/rtc.h"
#include "../../../kernel/drivers/serial.h"
#include "../terminal/terminal.h"
#include "../../../apps/notepad/notepad.h"
#include "../../shell/commands.h"
#include "../../icons/network.h"
#include "../../icons/sound.h"
#include "../../../kernel/hal/net.h"

static Window* windows[WM_MAX_WINDOWS];
static uint32_t window_directories[WM_MAX_WINDOWS];
static void (*invalidate)(void);
static os_api_t* wm_api;
static int32_t mouse_x = 400;
static int32_t mouse_y = 300;
static Window* dragging_window;
static Window* resizing_terminal;
static Window* resizing_user;
static uint32_t dragging_directory;
static uint8_t previous_left_button;
static uint8_t start_menu_open;
static uint8_t redraw_pending;
static uint32_t scene_generation;
/* One background and one retained image for the actively dragged window.
 * This keeps motion independent of legacy draw callbacks and other windows. */
static uint32_t* drag_background;
static uint32_t* drag_pixels;
static struct {
    Window* window;
    uint32_t directory, generation;
    int32_t x, y, w, h;
    uint8_t valid;
} drag_cache;

#define USER_WINDOW_EVENT_CAPACITY 16u
#define USER_WINDOW_TEXT_MAX 64u
#define USER_WINDOW_MAX_SURFACE_PAGES \
    ((SCREEN_MAX_WIDTH * SCREEN_MAX_HEIGHT * sizeof(uint32_t) + PAGE_SIZE - 1u) / PAGE_SIZE)

extern const uint8_t font_8x8[128][8];

typedef struct {
    Window window;
    char title[USER_WINDOW_TITLE_MAX];
    uint32_t owner_directory;
    uint32_t handle;
    user_window_event_t events[USER_WINDOW_EVENT_CAPACITY];
    uint32_t surface_frames[USER_WINDOW_MAX_SURFACE_PAGES];
    uint32_t visible_frames[USER_WINDOW_MAX_SURFACE_PAGES];
    uint16_t visible_page_count;
    uint8_t drawing_frame;
    uint16_t surface_page_count;
    uint8_t event_read;
    uint8_t event_write;
    uint8_t active;
    uint8_t resizable, fullscreen;
    int32_t restore_x, restore_y, restore_w, restore_h;
    uint8_t raw_keys;
    uint8_t keys_reset_pending;
} user_window_slot_t;

static user_window_slot_t user_window_slots[WM_MAX_WINDOWS];
static uint32_t next_user_window_handle = 1;

static user_window_slot_t* user_slot_for_window(Window* window) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        if (user_window_slots[i].active && &user_window_slots[i].window == window)
            return &user_window_slots[i];
    return 0;
}

static user_window_slot_t* user_slot_by_handle(uint32_t handle,
                                                uint32_t owner_directory) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        user_window_slot_t* slot = &user_window_slots[i];
        if (slot->active && slot->handle == handle &&
            slot->owner_directory == owner_directory) return slot;
    }
    return 0;
}

static void user_window_enqueue(user_window_slot_t* slot,
                                const user_window_event_t* event) {
    if (!slot || !event) return;
    uint8_t next = (uint8_t)((slot->event_write + 1u) % USER_WINDOW_EVENT_CAPACITY);
    if (next == slot->event_read && slot->raw_keys) slot->keys_reset_pending = 1;
    if (next == slot->event_read)
        slot->event_read = (uint8_t)((slot->event_read + 1u) % USER_WINDOW_EVENT_CAPACITY);
    slot->events[slot->event_write] = *event;
    slot->event_write = next;
}

static void user_window_enqueue_simple(user_window_slot_t* slot, uint32_t type) {
    user_window_event_t event = {0};
    event.type = type;
    if (slot && type == USER_WINDOW_EVENT_REDRAW) {
        event.x = slot->window.x;
        event.y = slot->window.y;
    }
    user_window_enqueue(slot, &event);
}

static uint32_t* user_surface_pixel(user_window_slot_t* slot, uint32_t x,
                                    uint32_t y) {
    if (!slot) return 0;
    uint32_t pixel_index = y * (uint32_t)slot->window.w + x;
    uint32_t byte_offset = pixel_index * sizeof(uint32_t);
    uint32_t page = byte_offset / PAGE_SIZE;
    uint32_t offset = byte_offset & (PAGE_SIZE - 1u);
    if (page >= slot->surface_page_count) return 0;
    return (uint32_t*)(slot->surface_frames[page] + offset);
}

static void user_surface_set_pixel(user_window_slot_t* slot, int32_t x,
                                   int32_t y, uint32_t color) {
    if (!slot || x < 0 || y < 0 || x >= slot->window.w || y >= slot->window.h)
        return;
    uint32_t* pixel = user_surface_pixel(slot, (uint32_t)x, (uint32_t)y);
    if (pixel) *pixel = color;
}

static void user_surface_fill(user_window_slot_t* slot, int32_t x, int32_t y,
                              int32_t width, int32_t height, uint32_t color) {
    if (!slot || width <= 0 || height <= 0) return;
    if (x < 0) {
        if (x <= -width) return;
        width += x;
        x = 0;
    }
    if (y < 18) {
        if (y <= 18 - height) return;
        height -= 18 - y;
        y = 18;
    }
    if (x >= slot->window.w || y >= slot->window.h) return;
    if (width > slot->window.w - x) width = slot->window.w - x;
    if (height > slot->window.h - y) height = slot->window.h - y;
    for (int32_t py = 0; py < height; py++)
        for (int32_t px = 0; px < width; px++)
            user_surface_set_pixel(slot, x + px, y + py, color);
}

static void user_surface_text(user_window_slot_t* slot, int32_t x, int32_t y,
                              const char* text, uint32_t color,
                              uint32_t max_chars) {
    if (!slot || !text || x < 0 || y < 18 || x >= slot->window.w ||
        y > slot->window.h - 8) return;
    for (uint32_t i = 0; i < max_chars && text[i]; i++) {
        uint8_t ch = (uint8_t)text[i];
        if (ch >= 128) ch = '?';
        for (int32_t row = 0; row < 8; row++) {
            uint8_t bits = font_8x8[ch][row];
            for (int32_t col = 0; col < 8; col++) {
                if (bits & (0x80u >> col))
                    user_surface_set_pixel(slot, x + (int32_t)i * 8 + col,
                                           y + row, color);
            }
        }
    }
}

static void user_surface_composite(user_window_slot_t* slot) {
    if (!slot) return;
    Window* window = &slot->window;
    /* The compositor already owns kernel_mutex. Copy page-sized row spans
     * directly, avoiding a mutex/IRQ transition for every individual pixel. */
    int32_t left = window->x < 0 ? -window->x : (slot->fullscreen ? 0 : 1);
    int32_t right = window->w - (slot->fullscreen ? 0 : 1);
    if (window->x + right > SCREEN_WIDTH) right = SCREEN_WIDTH - window->x;
    if (left >= right) return;
    uint32_t* buffer = display_get_backbuffer();
    for (int32_t y = slot->fullscreen ? 0 : 18; y < window->h - (slot->fullscreen ? 0 : 1); y++) {
        int32_t py = window->y + y;
        if (py < 0 || py >= SCREEN_HEIGHT) continue;
        int32_t x = left;
        while (x < right) {
            uint32_t offset = ((uint32_t)y * window->w + x) * sizeof(uint32_t);
            uint32_t count = (PAGE_SIZE - (offset & (PAGE_SIZE - 1u))) / sizeof(uint32_t);
            if (count > (uint32_t)(right - x)) count = (uint32_t)(right - x);
            const uint32_t* source = user_surface_pixel(slot, (uint32_t)x, (uint32_t)y);
            if (slot->drawing_frame)
                source = (const uint32_t*)(slot->visible_frames[offset / PAGE_SIZE] +
                                           (offset & (PAGE_SIZE - 1u)));
            if (!source) break;
            uint32_t* dest = buffer + py * SCREEN_WIDTH + window->x + x;
            uint32_t copied = count;
            __asm__ volatile ("cld; rep movsl" : "+S"(source), "+D"(dest), "+c"(copied) : : "memory");
            x += count;
        }
    }
}

static void user_surface_release(user_window_slot_t* slot) {
    if (!slot) return;
    for (uint32_t i = 0; i < slot->surface_page_count; i++)
        paging_frame_free(slot->surface_frames[i]);
    for (uint32_t i = 0; i < slot->visible_page_count; i++)
        paging_frame_free(slot->visible_frames[i]);
    slot->visible_page_count = 0;
    slot->drawing_frame = 0;
    slot->surface_page_count = 0;
}

void request_redraw(void) {
    scene_generation++;
    redraw_pending = 1;
    if (invalidate) invalidate();
}

void wm_invalidate(void) {
    MUTEX_GUARD(kernel_mutex);
    request_redraw();
}

static uint32_t window_context_enter(int index) {
    return app_loader_context_enter(window_directories[index]);
}

static void window_context_leave(uint32_t previous) {
    app_loader_context_leave(previous);
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
        user_window_slot_t* slot = user_slot_for_window(windows[i]);
        if (i != target_index && windows[i]->focused && slot && slot->raw_keys)
            slot->keys_reset_pending = 1;
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
    uint32_t pages = (SCREEN_MAX_WIDTH * SCREEN_MAX_HEIGHT * 4u + PAGE_SIZE - 1u) / PAGE_SIZE;
    if (!drag_background) drag_background = (uint32_t*)paging_alloc_contiguous(pages);
    if (!drag_pixels) drag_pixels = (uint32_t*)paging_alloc_contiguous(pages);

    MUTEX_GUARD(kernel_mutex);
    wm_api = api;
    invalidate = invalidate_callback;
    start_menu_init(api);
}

static void draw_circle_filled(os_api_t* api, int32_t cx, int32_t cy, int32_t r, uint32_t color) {
    for (int32_t dy = -r; dy <= r; dy++) {
        for (int32_t dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                api->graphics.put_pixel(cx + dx, cy + dy, color);
            }
        }
    }
}

void wm_draw_native_frame(int32_t x, int32_t y, int32_t w, int32_t h,
                          const char* title, uint8_t focused) {
    if (!wm_api || w < 40 || h < 30) return;

    const int TITLE_H = 22;

    wm_api->graphics.fill_rect(x, y + TITLE_H, w, h - TITLE_H, COLOR_SURFACE);

    uint32_t tb_color = focused ? COLOR_TITLEBAR_ACTIVE : COLOR_TITLEBAR_INACTIVE;
    wm_api->graphics.fill_rect(x, y, w, TITLE_H, tb_color);

    wm_api->graphics.fill_rect(x, y + TITLE_H - 1, w, 1, COLOR_SURFACE_BORDER);

    int btn_cy = y + (TITLE_H / 2);
    int close_cx = x + w - 14;

    if (focused) {
        draw_circle_filled(wm_api, close_cx, btn_cy, 5, COLOR_BTN_CLOSE);
    } else {
        draw_circle_filled(wm_api, close_cx, btn_cy, 5, COLOR_BTN_MUTED);
    }

    if (title) {
        uint32_t text_col = focused ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED;
        wm_api->graphics.draw_string(x + 12, y + 7, title, text_col);
    }

    uint32_t border_col = focused ? COLOR_ACCENT : COLOR_SURFACE_BORDER;
    wm_api->graphics.fill_rect(x, y, w, 1, border_col);
    wm_api->graphics.fill_rect(x, y + h - 1, w, 1, border_col);
    wm_api->graphics.fill_rect(x, y, 1, h, border_col);
    wm_api->graphics.fill_rect(x + w - 1, y, 1, h, border_col);
}

static void draw_window_frame(Window* window) {
    if (!window || !window->visible) return;
    user_window_slot_t* user_slot = user_slot_for_window(window);
    if (!user_slot || !user_slot->fullscreen)
        wm_draw_native_frame(window->x, window->y, window->w, window->h,
                             window->title, window->focused);
    if (user_slot) {
        user_surface_composite(user_slot);
        if (user_slot->resizable && !user_slot->fullscreen)
            for(int i=3;i<12;i+=3)
                wm_api->graphics.fill_rect(window->x+window->w-i,window->y+window->h-3,i-1,1,COLOR_ACCENT);
    } else if (window->draw_content) {
        window->draw_content(window);
    }
}

static void draw_windows_except(Window* excluded) {
    MUTEX_GUARD(kernel_mutex);
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!windows[i] || windows[i] == excluded) continue;
        uint32_t previous = window_context_enter(i);
        draw_window_frame(windows[i]);
        window_context_leave(previous);
    }
}

void wm_draw_windows(void) { draw_windows_except(0); }

static void copy_pixels(uint32_t* dest, const uint32_t* source, uint32_t count) {
    __asm__ volatile ("cld; rep movsl" : "+D"(dest), "+S"(source), "+c"(count) : : "memory");
}

/* Restore/paste only a clipped window footprint; taskbar pixels stay intact. */
static void drag_restore(int32_t x, int32_t y, int32_t w, int32_t h) {
    int32_t left = x < 0 ? 0 : x, top = y < 0 ? 0 : y;
    int32_t right = x + w < SCREEN_WIDTH ? x + w : SCREEN_WIDTH;
    int32_t bottom = y + h < WM_TASKBAR_Y ? y + h : WM_TASKBAR_Y;
    uint32_t* buffer = display_get_backbuffer();
    if (left >= right || top >= bottom) return;
    for (int32_t row = top; row < bottom; row++)
        copy_pixels(buffer + row * SCREEN_WIDTH + left,
                    drag_background + row * SCREEN_WIDTH + left, right - left);
}

uint8_t wm_render_drag(void) {
    if (!drag_background || !drag_pixels) return 0;
    MUTEX_GUARD(kernel_mutex);
    int index = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        if (dragging_window && windows[i] == dragging_window &&
            window_directories[i] == dragging_directory) { index = i; break; }
    if (index < 0) { drag_cache.valid = 0; return 0; }
    uint32_t previous = window_context_enter(index);
    Window* w = windows[index];
    int32_t x = w->x, y = w->y, width = w->w, height = w->h;
    uint8_t visible = w->visible;
    window_context_leave(previous);
    if (!visible || width <= 0 || width > SCREEN_WIDTH ||
        height <= 0 || height > WM_TASKBAR_Y) { drag_cache.valid = 0; return 0; }
    uint32_t* buffer = display_get_backbuffer();
    if (!drag_cache.valid || drag_cache.window != w ||
        drag_cache.directory != dragging_directory || drag_cache.generation != scene_generation ||
        drag_cache.w != width || drag_cache.h != height) {
        drag_cache.valid = 0;
        /* Capture a complete window. An initially clipped window uses the
         * regular compositor until it is fully visible. */
        if (x < 0 || y < 0 || x + width > SCREEN_WIDTH || y + height > WM_TASKBAR_Y) return 0;
        //display_clear(COLOR_DESKTOP);
        desktop_wallpaper_draw();
        desktop_draw();
        draw_windows_except(w);
        wm_draw_taskbar(start_menu_open);
        copy_pixels(drag_background, buffer, SCREEN_WIDTH * SCREEN_HEIGHT);
        previous = window_context_enter(index);
        draw_window_frame(w);
        window_context_leave(previous);
        for (int32_t row = 0; row < height; row++)
            copy_pixels(drag_pixels + row * width, buffer + (y + row) * SCREEN_WIDTH + x, width);
        drag_cache.window = w; drag_cache.directory = dragging_directory;
        drag_cache.generation = scene_generation;
        drag_cache.x = x; drag_cache.y = y;
        drag_cache.w = width; drag_cache.h = height; drag_cache.valid = 1;
        display_blit();
        wm_cursor_draw();
        return 1;
    }
    int32_t old_x = drag_cache.x, old_y = drag_cache.y;
    drag_restore(old_x, old_y, width, height);
    int32_t left = x < 0 ? 0 : x, top = y < 0 ? 0 : y;
    int32_t right = x + width < SCREEN_WIDTH ? x + width : SCREEN_WIDTH;
    int32_t bottom = y + height < WM_TASKBAR_Y ? y + height : WM_TASKBAR_Y;
    if (left < right && top < bottom)
        for (int32_t row = top; row < bottom; row++)
            copy_pixels(buffer + row * SCREEN_WIDTH + left,
                        drag_pixels + (row - y) * width + left - x, right - left);
    /* The VRAM copy includes the old and new footprints, not the full screen.
     * Merge overlapping footprints to avoid copying their intersection twice. */
    wm_cursor_erase();
    if (x < old_x + width && old_x < x + width && y < old_y + height && old_y < y + height) {
        int32_t bx = x < old_x ? x : old_x, by = y < old_y ? y : old_y;
        int32_t ex = (x > old_x ? x : old_x) + width;
        int32_t ey = (y > old_y ? y : old_y) + height;
        if (ey > WM_TASKBAR_Y) ey = WM_TASKBAR_Y;
        display_blit_rect(bx, by, ex - bx, ey - by);
    } else {
        int32_t oh = old_y + height > WM_TASKBAR_Y ? WM_TASKBAR_Y - old_y : height;
        int32_t nh = y + height > WM_TASKBAR_Y ? WM_TASKBAR_Y - y : height;
        display_blit_rect(old_x, old_y, width, oh);
        display_blit_rect(x, y, width, nh);
    }
    drag_cache.x = x; drag_cache.y = y;
    wm_cursor_draw();
    return 1;
}


static int volume_open, volume_drag, volume_busy;
#define VOLUME_X (SCREEN_WIDTH - 270)
#define VOLUME_Y (WM_TASKBAR_Y - 174)
static void volume_draw(void) {
    if (!volume_open) return;
    int x = VOLUME_X, y = VOLUME_Y;
    wm_api->graphics.draw_button(x, y, 264, 170, "", 0);
    char label[] = "Volume: 100%";
    uint32_t v = audio_get_volume();
    label[8] = v == 100 ? '1' : ' '; label[9] = v >= 10 ? '0' + v / 10 % 10 : ' ';
    label[10] = '0' + v % 10;
    wm_api->graphics.draw_string(x + 12, y + 12, label, COLOR_BLACK);
    wm_api->graphics.fill_rect(x + 16, y + 42, 200, 4, COLOR_DARK_GRAY);
    wm_api->graphics.draw_button(x + 12 + v * 2, y + 34, 9, 20, "", 0);
    wm_api->graphics.draw_button(x + 222, y + 30, 34, 26, v ? "M" : "U", !v);
    for (uint32_t i = 0; i < audio_device_count(); i++)
        wm_api->graphics.draw_button(x + 12, y + 64 + i * 26, 240, 24,
            audio_device_name(i), audio_device_selected() == i);
    wm_api->graphics.draw_string(x + 12, y + 146,
        volume_busy ? "Output unavailable" : "Output device", COLOR_BLACK);
}
static int volume_mouse(int x, int y, int left, int down) {
    if (!left) volume_drag = 0;
    if (down && x >= SCREEN_WIDTH - 104 && x < SCREEN_WIDTH - 82 &&
        y >= WM_TASKBAR_Y + 5) {
        volume_open = !volume_open; volume_busy = 0; start_menu_close(); return 1;
    }
    if (!volume_open) return 0;
    int inside = x >= VOLUME_X && x < VOLUME_X + 264 &&
        y >= VOLUME_Y && y < VOLUME_Y + 170;
    if (down && !inside) { volume_open = 0; request_redraw(); return 0; }
    if (down && inside) {
        if (y >= VOLUME_Y + 30 && y < VOLUME_Y + 56) {
            if (x >= VOLUME_X + 222) {
                static uint32_t saved = 100;
                uint32_t v = audio_get_volume();
                if (v) { saved = v; audio_set_volume(0); }
                else audio_set_volume(saved);
            } else volume_drag = 1;
        }
        for (uint32_t i = 0; i < audio_device_count(); i++)
            if (y >= VOLUME_Y + 64 + (int)i * 26 && y < VOLUME_Y + 88 + (int)i * 26)
                volume_busy = audio_mixer_device_select(i) < 0;
    }
    if (volume_drag && left) {
        int value = (x - VOLUME_X - 16) / 2;
        audio_set_volume(value < 0 ? 0 : value > 100 ? 100 : value);
    }
    return inside || volume_drag;
}

void wm_draw_taskbar(uint8_t start_menu_open) {
    MUTEX_GUARD(kernel_mutex);
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        user_window_slot_t* slot = user_slot_for_window(windows[i]);
        if (slot && slot->fullscreen && slot->window.visible && slot->window.focused)
            return;
    }
    if (!wm_api) return;
    const int y = WM_TASKBAR_Y;

    /* Base panel */
    wm_api->graphics.fill_rect(0, y, SCREEN_WIDTH, WM_TASKBAR_HEIGHT, COLOR_TASKBAR_BG);
    wm_api->graphics.fill_rect(0, y, SCREEN_WIDTH, 1, COLOR_TASKBAR_BORDER);

    /* Start Button: Clean Flat Pill */
    uint32_t start_bg = start_menu_is_open() ? COLOR_ACCENT : COLOR_TASKBAR_ITEM;
    uint32_t start_text = start_menu_is_open() ? 0x0011111B : COLOR_TEXT_PRIMARY;
    wm_api->graphics.fill_rect(6, y + 4, 60, 24, start_bg);
    /* 1px border */
    wm_api->graphics.fill_rect(6, y + 4, 60, 1, COLOR_SURFACE_BORDER);
    wm_api->graphics.fill_rect(6, y + 27, 60, 1, COLOR_SURFACE_BORDER);
    wm_api->graphics.fill_rect(6, y + 4, 1, 24, COLOR_SURFACE_BORDER);
    wm_api->graphics.fill_rect(65, y + 4, 1, 24, COLOR_SURFACE_BORDER);
    wm_api->graphics.draw_string(18, y + 11, "Menu", start_text);

    /* Running Tasks (Pills) */
    int count = 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) if (windows[i]) count++;
    int width = count ? (540 / count) : 120;
    if (width > 130) width = 130;

    int button = 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!windows[i]) continue;
        uint32_t previous = window_context_enter(i);
        char label[24];
        int capacity = (width - 16) / 8;
        if (capacity > 23) capacity = 23;
        int n = 0;
        while (n < capacity && windows[i]->title[n]) { label[n] = windows[i]->title[n]; n++; }
        label[n] = 0;

        int bx = 74 + button * (width + 4);
        int by = y + 4;
        uint8_t is_focused = windows[i]->visible && windows[i]->focused;

        /* Pill Background */
        wm_api->graphics.fill_rect(bx, by, width, 24, is_focused ? COLOR_TASKBAR_ITEM_ACT : COLOR_TASKBAR_ITEM);
        
        /* Highlight line on active item */
        if (is_focused) {
            wm_api->graphics.fill_rect(bx + 4, by + 22, width - 8, 2, COLOR_ACCENT);
        }

        /* Label */
        wm_api->graphics.draw_string(bx + 8, by + 8, label, is_focused ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED);

        window_context_leave(previous);
        button++;
    }

    /* Modern Minimalist Tray Area */
    const int tray_w = 90;
    const int tray_x = SCREEN_WIDTH - tray_w - 8;
    const int tray_y = y + 5;
    
    /* Clean rounded/flat tray container */
    wm_api->graphics.fill_rect(tray_x, tray_y, tray_w, 22, COLOR_TASKBAR_ITEM);
    wm_api->graphics.fill_rect(tray_x, tray_y, tray_w, 1, COLOR_SURFACE_BORDER);

    /* Tray icons (Sound, Network) */
    const int sound_x = tray_x + 6;
    const int sound_y = tray_y + 3;
    for (int r = 0; r < SOUND_ICON_H; r++) {
        for (int c = 0; c < SOUND_ICON_W; c++) {
            uint8_t val = icon_sound[r][c];
            if (val != 0) {
                wm_api->graphics.put_pixel(sound_x + c, sound_y + r, sound_icon_color(val));
            }
        }
    }

    uint8_t net_ok = sys_net_is_ready();
    const uint8_t (*net_sprite)[NET_ICON_W] = net_ok ? icon_net_ok : icon_net_err;
    const int icon_x = tray_x + 26;
    const int icon_y = tray_y + 3;
    for (int r = 0; r < NET_ICON_H; r++) {
        for (int c = 0; c < NET_ICON_W; c++) {
            uint8_t val = net_sprite[r][c];
            if (val != 0) {
                wm_api->graphics.put_pixel(icon_x + c, icon_y + r, net_icon_color(val));
            }
        }
    }

    /* Clock */
    char time_str[6];
    rtc_format_time_hm(time_str);
    wm_api->graphics.draw_string(tray_x + 48, y + 11, time_str, COLOR_TEXT_PRIMARY);

    volume_draw();
    if (!start_menu_open) return;
    start_menu_draw();
}

Window* wm_get_window(int index) {
    MUTEX_GUARD(kernel_mutex);
    if (index < 0 || index >= WM_MAX_WINDOWS) return 0;
    return windows[index];
}

void wm_bring_to_front(Window* target) {
    MUTEX_GUARD(kernel_mutex);
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
    MUTEX_GUARD(kernel_mutex);
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
                user_window_slot_t* old_slot = user_slot_for_window(windows[j]);
                if (old_slot && old_slot->raw_keys) old_slot->keys_reset_pending = 1;
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

int32_t wm_user_window_create(const user_window_desc_t* description,
                              uint32_t owner_directory) {
    MUTEX_GUARD(kernel_mutex);
    if (!description || !owner_directory || !description->title[0] ||
        description->width < 80 || description->height < 50 ||
        description->width > SCREEN_WIDTH || description->height > WM_TASKBAR_Y ||
        description->x < 0 || description->y < 0 ||
        description->x > SCREEN_WIDTH - description->width ||
        description->y > WM_TASKBAR_Y - description->height)
        return -1;

    int window_index = -1;
    int slot_index = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!windows[i] && window_index < 0) window_index = i;
        if (!user_window_slots[i].active && slot_index < 0) slot_index = i;
    }
    if (window_index < 0 || slot_index < 0) return -1;

    user_window_slot_t* slot = &user_window_slots[slot_index];
    slot->resizable = slot->fullscreen = slot->raw_keys = slot->keys_reset_pending = 0;
    slot->active = 1;
    slot->owner_directory = owner_directory;
    slot->handle = next_user_window_handle++;
    if (!slot->handle || slot->handle > 0x7FFFFFFFu) {
        next_user_window_handle = 2;
        slot->handle = 1;
    }
    uint32_t title_index = 0;
    while (title_index < USER_WINDOW_TITLE_MAX - 1u && description->title[title_index]) {
        slot->title[title_index] = description->title[title_index];
        title_index++;
    }
    slot->title[title_index] = '\0';
    slot->event_read = 0;
    slot->event_write = 0;
    slot->window.x = description->x;
    slot->window.y = description->y;
    slot->window.w = description->width;
    slot->window.h = description->height;
    slot->window.title = slot->title;
    slot->window.visible = 1;
    slot->window.focused = 1;
    slot->window.draw_content = 0;
    slot->window.handle_key = 0;
    slot->window.handle_mouse = 0;
    uint32_t surface_bytes = (uint32_t)slot->window.w *
                             (uint32_t)slot->window.h * sizeof(uint32_t);
    uint32_t page_count = (surface_bytes + PAGE_SIZE - 1u) / PAGE_SIZE;
    if (!page_count || page_count > USER_WINDOW_MAX_SURFACE_PAGES) {
        slot->active = 0;
        return -1;
    }
    slot->surface_page_count = 0;
    for (uint32_t i = 0; i < page_count; i++) {
        uint32_t frame = paging_frame_alloc();
        if (!frame) {
            user_surface_release(slot);
            slot->active = 0;
            return -1;
        }
        slot->surface_frames[slot->surface_page_count++] = frame;
    }
    user_surface_fill(slot, 0, 18, slot->window.w, slot->window.h - 18,
                      COLOR_WIN_BG);

    windows[window_index] = &slot->window;
    window_directories[window_index] = owner_directory;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (i == window_index || !windows[i]) continue;
        uint32_t previous = window_context_enter(i);
        user_window_slot_t* old_slot = user_slot_for_window(windows[i]);
        if (old_slot && old_slot->raw_keys) old_slot->keys_reset_pending = 1;
        windows[i]->focused = 0;
        window_context_leave(previous);
    }
    app_loader_window_attached(owner_directory);
    wm_bring_to_front_index(window_index);
    user_window_enqueue_simple(slot, USER_WINDOW_EVENT_REDRAW);
    request_redraw();
    return (int32_t)slot->handle;
}

/* Grow before changing geometry so allocation failure leaves the old window usable. */
static int user_window_resize(user_window_slot_t* slot, int w, int h) {
    if (w < 160 || h < 118 || w > SCREEN_WIDTH || h > SCREEN_HEIGHT)
        return -1;
    uint32_t pages = ((uint32_t)w * h * 4u + PAGE_SIZE - 1u) / PAGE_SIZE;
    if (pages > USER_WINDOW_MAX_SURFACE_PAGES)
        return -1;
    uint32_t old_count = slot->surface_page_count;
    while (slot->surface_page_count < pages) {
        uint32_t frame = paging_frame_alloc();
        if (!frame) {
            while (slot->surface_page_count > old_count)
                paging_frame_free(slot->surface_frames[--slot->surface_page_count]);
            return -1;
        }
        slot->surface_frames[slot->surface_page_count++] = frame;
    }
    for (uint32_t i = 0; i < slot->visible_page_count; i++)
        paging_frame_free(slot->visible_frames[i]);
    slot->visible_page_count = 0;
    slot->drawing_frame = 0;
    slot->window.w = w;
    slot->window.h = h;
    user_surface_fill(slot, 0, 18, w, h - 18, 0);
    user_window_enqueue_simple(slot, USER_WINDOW_EVENT_REDRAW);
    request_redraw();
    return 0;
}

static int user_window_fullscreen(user_window_slot_t* slot, int enabled) {
    Window* w = &slot->window;
    if (enabled == slot->fullscreen)
        return 0;
    int width = enabled ? SCREEN_WIDTH : slot->restore_w;
    int height = enabled ? SCREEN_HEIGHT : slot->restore_h;
    if (!enabled) {
        if (width > SCREEN_WIDTH)
            width = SCREEN_WIDTH;
        if (height > WM_TASKBAR_Y)
            height = WM_TASKBAR_Y;
    }
    int x = w->x, y = w->y, old_w = w->w, old_h = w->h;
    if (user_window_resize(slot, width, height) < 0)
        return -1;
    if (enabled) {
        slot->restore_x = x;
        slot->restore_y = y;
        slot->restore_w = old_w;
        slot->restore_h = old_h;
        w->x = w->y = 0;
    } else {
        w->x = slot->restore_x;
        w->y = slot->restore_y;
        if (w->x > SCREEN_WIDTH - w->w)
            w->x = SCREEN_WIDTH - w->w;
        if (w->y > WM_TASKBAR_Y - w->h)
            w->y = WM_TASKBAR_Y - w->h;
    }
    slot->fullscreen = enabled;
    return 0;
}

int32_t wm_user_window_draw(uint32_t handle, uint32_t owner_directory,
                            const user_window_draw_t* drawing, const char* text,
                            const uint8_t* image_data) {
    MUTEX_GUARD(kernel_mutex);
    user_window_slot_t* slot = user_slot_by_handle(handle, owner_directory);
    if (!slot || !drawing || !wm_api)
        return -1;
    Window* window = &slot->window;

    if (drawing->operation == USER_WINDOW_RESIZABLE) {
        slot->resizable = drawing->pressed != 0;
        return 0;
    }
    if (drawing->operation == USER_WINDOW_FULLSCREEN)
        return user_window_fullscreen(slot, drawing->pressed != 0);
    if (drawing->operation == USER_WINDOW_DRAW_PIXELS_SCALED) {
        if (!image_data || drawing->width <= 0 || drawing->width > 640 || drawing->height <= 0 ||
            drawing->height > 480 ||
            drawing->data_length != (uint32_t)drawing->width * drawing->height * 4u)
            return -1;
        int top = slot->fullscreen ? 0 : 18, border = slot->fullscreen ? 0 : 1;
        int cw = window->w - 2 * border, ch = window->h - top - border;
        int dw = cw, dh = cw * drawing->height / drawing->width;
        if (dh > ch) {
            dh = ch;
            dw = ch * drawing->width / drawing->height;
        }
        if (dw <= 0 || dh <= 0)
            return -1;
        int ox = border + (cw - dw) / 2, oy = top + (ch - dh) / 2;
        const uint32_t* pixels = (const uint32_t*)image_data;
        /* The WM mutex serializes this scratch table; no per-pixel division. */
        static uint16_t source_x[SCREEN_MAX_WIDTH];
        for (int x = 0; x < dw; x++)
            source_x[x] = x * drawing->width / dw;
        for (int y = top; y < window->h - border; y++) {
            int source_row =
                (y >= oy && y < oy + dh) ? ((y - oy) * drawing->height / dh) * drawing->width : 0;
            for (int x = border; x < window->w - border; x++) {
                uint32_t color = 0;
                if (x >= ox && x < ox + dw && y >= oy && y < oy + dh)
                    color = pixels[source_row + source_x[x - ox]];
                user_surface_set_pixel(slot, x, y, color & 0xffffffu);
            }
        }
        return 0;
    }
    if (drawing->operation == USER_WINDOW_RAW_KEYS) {
        slot->raw_keys = drawing->pressed != 0;
        return 0;
    }
    if (drawing->operation == USER_WINDOW_DRAW_PIXELS) {
        if (!image_data || drawing->width <= 0 || drawing->height <= 0 || drawing->width > 640 ||
            drawing->height > 480 || drawing->x < 0 || drawing->y < 18 ||
            drawing->x > window->w - drawing->width || drawing->y > window->h - drawing->height ||
            drawing->data_length != (uint32_t)drawing->width * drawing->height * 4u)
            return -1;
        const uint32_t* pixels = (const uint32_t*)image_data;
        for (int y = 0; y < drawing->height; y++)
            for (int x = 0; x < drawing->width; x++)
                user_surface_set_pixel(slot, drawing->x+x, drawing->y+y,
                    pixels[y*drawing->width+x] & 0xffffffu);
        return 0;
    }
    if (drawing->operation == USER_WINDOW_3D_SUPPORTED)
        return video_3d_available();
    if (drawing->operation == USER_WINDOW_DRAW_TRIANGLES) {
        if (!image_data || !drawing->data_length ||
            drawing->data_length % sizeof(user_3d_triangle_t) ||
            drawing->data_length / sizeof(user_3d_triangle_t) > USER_3D_MAX_TRIANGLES ||
            drawing->x < 0 || drawing->y < 18 ||
            drawing->width <= 0 || drawing->height <= 0 ||
            drawing->width > (int32_t)USER_3D_MAX_WIDTH ||
            drawing->height > (int32_t)USER_3D_MAX_HEIGHT ||
            drawing->x > window->w - drawing->width ||
            drawing->y > window->h - drawing->height) return -1;
        const user_3d_triangle_t* triangles = (const user_3d_triangle_t*)image_data;
        uint32_t count = drawing->data_length / sizeof(*triangles);
        for (uint32_t i = 0; i < count; i++) for (uint32_t j = 0; j < 3; j++)
            if (triangles[i].p[j].x < 0 || triangles[i].p[j].y < 0 ||
                triangles[i].p[j].x >= drawing->width ||
                triangles[i].p[j].y >= drawing->height) return -1;
        if (!video_3d_available()) return 0;
        uint32_t stride;
        const uint32_t* pixels = video_3d_render(triangles, count, drawing->width,
            drawing->height, drawing->color, &stride);
        if (!pixels) return -1;
        for (int32_t y = 0; y < drawing->height; y++)
            for (int32_t x = 0; x < drawing->width; x++)
                user_surface_set_pixel(slot, drawing->x + x, drawing->y + y,
                    pixels[(uint32_t)y * stride + (uint32_t)x] & 0xffffffu);
        return 1;
    }
    if (drawing->operation == USER_WINDOW_DRAW_BEGIN) {
        if (slot->drawing_frame) return 0;
        /* Allocate lazily; legacy clients retain their single surface. */
        while (slot->visible_page_count < slot->surface_page_count) {
            uint32_t frame = paging_frame_alloc();
            if (!frame) {
                for (uint32_t i = 0; i < slot->visible_page_count; i++)
                    paging_frame_free(slot->visible_frames[i]);
                slot->visible_page_count = 0;
                return -1;
            }
            slot->visible_frames[slot->visible_page_count++] = frame;
        }
        for (uint32_t i = 0; i < slot->surface_page_count; i++) {
            const uint32_t* source = (const uint32_t*)slot->surface_frames[i];
            uint32_t* dest = (uint32_t*)slot->visible_frames[i];
            uint32_t count = PAGE_SIZE / sizeof(uint32_t);
            __asm__ volatile ("cld; rep movsl" : "+S"(source), "+D"(dest), "+c"(count) : : "memory");
        }
        slot->drawing_frame = 1;
        return 0;
    }

    if (drawing->operation == USER_WINDOW_DRAW_FILL) {
        user_surface_fill(slot, drawing->x, drawing->y, drawing->width,
                          drawing->height, drawing->color);
        return 0;
    }

    if (drawing->operation == USER_WINDOW_DRAW_BUTTON) {
        if (drawing->x < 0 || drawing->y < 18 || drawing->width <= 0 ||
            drawing->height <= 0 || drawing->x > window->w - drawing->width ||
            drawing->y > window->h - drawing->height) return -1;
        uint32_t max_chars = drawing->width > 12
            ? (uint32_t)(drawing->width - 12) / 8u : 0;
        if (max_chars > USER_WINDOW_TEXT_MAX - 1u)
            max_chars = USER_WINDOW_TEXT_MAX - 1u;
        uint32_t background = drawing->pressed ? COLOR_TASKBAR_ITEM_ACT : COLOR_SURFACE;
        uint32_t border = drawing->pressed ? COLOR_ACCENT : COLOR_SURFACE_BORDER;
        uint32_t text_color = drawing->pressed ? COLOR_ACCENT : COLOR_TEXT_PRIMARY;
        user_surface_fill(slot, drawing->x, drawing->y, drawing->width,
                          drawing->height, background);
        user_surface_fill(slot, drawing->x, drawing->y, drawing->width, 1, border);
        user_surface_fill(slot, drawing->x, drawing->y, 1, drawing->height, border);
        user_surface_fill(slot, drawing->x, drawing->y + drawing->height - 1,
                          drawing->width, 1, border);
        user_surface_fill(slot, drawing->x + drawing->width - 1, drawing->y,
                          1, drawing->height, border);
        uint32_t length = 0;
        while (text && length < max_chars && text[length]) length++;
        user_surface_text(slot, drawing->x + (drawing->width - (int32_t)length * 8) / 2,
                          drawing->y + (drawing->height - 8) / 2,
                          text, text_color, length);
        return 0;
    }

    if (drawing->operation == USER_WINDOW_DRAW_TEXT) {
        if (!text || drawing->x < 0 || drawing->y < 18 ||
            drawing->x >= window->w || drawing->y > window->h - 8) return -1;
        uint32_t max_chars = (uint32_t)(window->w - drawing->x) / 8u;
        if (max_chars > USER_WINDOW_TEXT_MAX - 1u)
            max_chars = USER_WINDOW_TEXT_MAX - 1u;
        uint32_t length = 0;
        while (length < max_chars && text[length]) {
            length++;
        }
        user_surface_text(slot, drawing->x, drawing->y, text, drawing->color,
                          length);
        return 0;
    }

    if (drawing->operation == USER_WINDOW_DRAW_TEXT_SCALED) {
        if (!text || drawing->width < 4 || drawing->width > 32 || drawing->height < 8 || drawing->height > 64 ||
            drawing->x < 0 || drawing->y < 18 || drawing->x >= window->w || drawing->y > window->h - drawing->height) return -1;
        unsigned advance = (unsigned)drawing->width, height = (unsigned)drawing->height;
        unsigned count = (unsigned)(window->w - drawing->x) / advance;
        if (count > USER_WINDOW_TEXT_MAX - 1) count = USER_WINDOW_TEXT_MAX - 1;
        for (unsigned i = 0; i < count && text[i]; i++) {
            unsigned char ch = (unsigned char)text[i]; if (ch >= 128) ch = '?';
            for (unsigned y = 0; y < height; y++) for (unsigned x = 0; x < advance; x++)
                if (font_8x8[ch][y * 8 / height] & (0x80u >> (x * 8 / advance)))
                    user_surface_set_pixel(slot, drawing->x + (int)(i * advance + x), drawing->y + (int)y, drawing->color);
        }
        return 0;
    }

    if (drawing->operation == USER_WINDOW_DRAW_IMAGE || drawing->operation == USER_WINDOW_DRAW_IMAGE_CLIPPED) {
        static uint32_t decoded[PNG_TARGET_W * PNG_TARGET_H];
        if (!image_data || !drawing->data_length ||
            drawing->width <= 0 || drawing->height <= 0 ||
            drawing->width > 512 || drawing->height > 512 ||
            !wm_api->graphics.png_decode_and_resize ||
            !wm_api->graphics.png_decode_and_resize(
                image_data, drawing->data_length, decoded)) return -1;
        int clip_top = 18, clip_bottom = window->h;
        if (drawing->operation == USER_WINDOW_DRAW_IMAGE_CLIPPED) {
            clip_top = (int)(drawing->color >> 16); clip_bottom = (int)(drawing->color & 65535u);
            if (clip_top < 18 || clip_bottom > window->h || clip_top >= clip_bottom) return -1;
        }
        for (int32_t y = 0; y < drawing->height; y++) {
            if (drawing->y + y < clip_top || drawing->y + y >= clip_bottom) continue;
            int32_t source_y = (y * PNG_TARGET_H) / drawing->height;
            for (int32_t x = 0; x < drawing->width; x++) {
                int32_t source_x = (x * PNG_TARGET_W) / drawing->width;
                uint32_t pixel = decoded[source_y * PNG_TARGET_W + source_x];
                unsigned alpha = pixel >> 24;
                if (!alpha) continue;
                if (alpha < 255) {
                    const uint32_t* under = user_surface_pixel(slot, (uint32_t)(drawing->x + x), (uint32_t)(drawing->y + y));
                    if (!under) continue;
                    unsigned rgb = 0;
                    for (unsigned shift = 0; shift <= 16; shift += 8)
                        rgb |= (((((pixel >> shift) & 255) * alpha + ((*under >> shift) & 255) * (255 - alpha) + 127) / 255) << shift);
                    pixel = rgb;
                }
                user_surface_set_pixel(slot, drawing->x + x, drawing->y + y, pixel & 0xffffffu);
            }
        }
        return 0;
    }
    return -1;
}

int32_t wm_user_window_poll(uint32_t handle, uint32_t owner_directory,
                            user_window_event_t* event) {
    MUTEX_GUARD(kernel_mutex);
    user_window_slot_t* slot = user_slot_by_handle(handle, owner_directory);
    if (!slot || !event) return -1;
    if (slot->keys_reset_pending) {
        slot->keys_reset_pending = 0;
        slot->event_read = slot->event_write;
        *event = (user_window_event_t){0};
        event->type = USER_WINDOW_EVENT_KEYS_RESET;
        return 1;
    }
    if (slot->event_read == slot->event_write) {
        event->type = USER_WINDOW_EVENT_NONE;
        return 0;
    }
    *event = slot->events[slot->event_read];
    slot->event_read = (uint8_t)((slot->event_read + 1u) % USER_WINDOW_EVENT_CAPACITY);
    return 1;
}

int32_t wm_user_window_present(uint32_t handle, uint32_t owner_directory) {
    MUTEX_GUARD(kernel_mutex);
    user_window_slot_t* slot = user_slot_by_handle(handle, owner_directory);
    if (!slot || !wm_api || !wm_api->graphics.blit_rect) return -1;
    slot->drawing_frame = 0;
    request_redraw();
    return 0;
}

static void user_window_remove_slot(user_window_slot_t* slot) {
    if (!slot) return;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (windows[i] != &slot->window) continue;
        for (int j = i; j < WM_MAX_WINDOWS - 1; j++) {
            windows[j] = windows[j + 1];
            window_directories[j] = window_directories[j + 1];
        }
        windows[WM_MAX_WINDOWS - 1] = 0;
        window_directories[WM_MAX_WINDOWS - 1] = 0;
        break;
    }
    uint32_t owner_directory = slot->owner_directory;
    if (resizing_user == &slot->window) resizing_user = 0;
    user_surface_release(slot);
    slot->active = 0;
    slot->owner_directory = 0;
    slot->handle = 0;
    app_loader_window_detached(owner_directory);
    request_redraw();
}

int32_t wm_user_window_close(uint32_t handle, uint32_t owner_directory) {
    MUTEX_GUARD(kernel_mutex);
    user_window_slot_t* slot = user_slot_by_handle(handle, owner_directory);
    if (!slot) return -1;
    user_window_remove_slot(slot);
    return 0;
}

void wm_user_close_address_space(uint32_t owner_directory) {
    MUTEX_GUARD(kernel_mutex);
    if (!owner_directory) return;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (user_window_slots[i].active &&
            user_window_slots[i].owner_directory == owner_directory)
            user_window_remove_slot(&user_window_slots[i]);
    }
    /* Legacy callback windows also belong to the executable's process. */
    if (paging_current_directory() == owner_directory) {
        for (int i = 0; i < WM_MAX_WINDOWS;) {
            if (windows[i] && window_directories[i] == owner_directory)
                wm_unregister_window(windows[i]);
            else i++;
        }
    }
}

void wm_unregister_window(Window* window) {
    MUTEX_GUARD(kernel_mutex);
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

void toggle_window(const char* title) {
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

    static uint8_t previous_right;
    uint8_t right = (event->mouse_buttons & 2) != 0;
    uint8_t right_down = right && !previous_right;
    previous_right = right;
    uint8_t left = event->mouse_buttons & 1;
    uint8_t click_down = left && !previous_left_button;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        user_window_slot_t* slot = user_slot_for_window(windows[i]);
        if (slot && slot->fullscreen && slot->window.focused && slot->window.visible) {
            if (wm_api->input.move_cursor)
                wm_api->input.move_cursor(mouse_x, mouse_y, 1);
            previous_left_button = left;
            return;
        }
    }
    if (volume_mouse(mouse_x, mouse_y, left, click_down)) {
        if (wm_api->input.move_cursor) wm_api->input.move_cursor(mouse_x, mouse_y, 1);
        previous_left_button = left;
        request_redraw();
        return;
    }
    uint8_t desktop_right = right_down && mouse_y < WM_TASKBAR_Y && !start_menu_is_open();
    if (desktop_right) {
        for (int i = 0; i < WM_MAX_WINDOWS; i++) {
            if (!windows[i]) continue;
            uint32_t previous = window_context_enter(i);
            Window* w = windows[i];
            if (w->visible && mouse_x >= w->x && mouse_x < w->x+w->w &&
                mouse_y >= w->y && mouse_y < w->y+w->h) desktop_right = 0;
            window_context_leave(previous);
        }
    }
    if ((desktop_overlay_active() || desktop_right) &&
        desktop_mouse(mouse_x, mouse_y, left, click_down, desktop_right)) {
        if (wm_api->input.move_cursor) wm_api->input.move_cursor(mouse_x, mouse_y, 1);
        previous_left_button = left;
        request_redraw();
        return;
    }

    if (event->mouse_dx || event->mouse_dy) {
        if (resizing_user && left) {
            user_window_slot_t* slot = user_slot_for_window(resizing_user);
            if (slot && slot->active && !slot->fullscreen) {
                int w = mouse_x - slot->window.x + 1, h = mouse_y - slot->window.y + 1;
                if (w < 160)
                    w = 160;
                if (h < 118)
                    h = 118;
                if (w > SCREEN_WIDTH - slot->window.x)
                    w = SCREEN_WIDTH - slot->window.x;
                if (h > WM_TASKBAR_Y - slot->window.y)
                    h = WM_TASKBAR_Y - slot->window.y;
                user_window_resize(slot, w, h);
            }
        }
        if (resizing_terminal && left) {
            int w = mouse_x - resizing_terminal->x + 1;
            int h = mouse_y - resizing_terminal->y + 1;
            int max_w = SCREEN_WIDTH - resizing_terminal->x;
            int max_h = WM_TASKBAR_Y - resizing_terminal->y;
            if (w < 184) w = 184;
            if (h < 110) h = 110;
            if (w > max_w) w = max_w;
            if (h > max_h) h = max_h;
            resizing_terminal->w = w;
            resizing_terminal->h = h;
            request_redraw();
        }
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
                redraw_pending = 1;
                break;
            }
        }
        if (wm_api->input.move_cursor)
            wm_api->input.move_cursor(mouse_x, mouse_y, dragging_window && left ? 0 : 1);
    }
    if (left != previous_left_button) request_redraw();

    if (!left) {
        /* Retained pixels move without repainting. Notify the application
         * once on drop so its cached window origin follows the final position. */
        if (dragging_window) {
            user_window_slot_t* slot = user_slot_for_window(dragging_window);
            if (slot) user_window_enqueue_simple(slot, USER_WINDOW_EVENT_REDRAW);
        }
        resizing_terminal = 0;
        resizing_user = 0;
        dragging_window = 0;
        dragging_directory = 0;
        previous_left_button = left;
        return;
    }

    if (click_down) {
    int handled = 0;
        if (start_menu_is_open() && start_menu_handle_mouse(mouse_x, mouse_y, click_down)) {
            handled = 1;
        }

        else if (mouse_x >= 4 && mouse_x <= 64 && mouse_y >= WM_TASKBAR_Y + 4 && mouse_y <= WM_TASKBAR_Y + 28) {
            start_menu_toggle();
            handled = 1;
            request_redraw();
        }
        else if (mouse_y >= WM_TASKBAR_Y + 4 && mouse_y <= WM_TASKBAR_Y + 28) {
            if (start_menu_is_open()) {
                start_menu_close();
            }

            int count = 0;
            for (int i = 0; i < WM_MAX_WINDOWS; i++) if (windows[i]) count++;
            int width = count ? 620 / count : 100;
            if (width > 100) width = 100;
            int button = 0;
            for (int i = 0; i < WM_MAX_WINDOWS; i++) {
                if (!windows[i]) continue;
                int x = 68 + button++ * width;
                if (mouse_x < x || mouse_x >= x + width - 3) continue;
                uint32_t previous = window_context_enter(i);
                /* Taskbar clicks select this instance; the close button ends it. */
                windows[i]->visible = 1;
                window_context_leave(previous);
                wm_bring_to_front_index(i);
                request_redraw(); handled = 1; break;
            }
        }

        if (!handled && start_menu_is_open()) {
            start_menu_close();
            request_redraw();
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
                user_window_slot_t* resize_slot = user_slot_for_window(window);
                if (resize_slot && resize_slot->fullscreen && mouse_x >= window->x &&
                    mouse_x < window->x + window->w && mouse_y >= window->y &&
                    mouse_y < window->y + window->h) {
                    window_context_leave(previous);
                    handled = 1;
                    break;
                }
                if (resize_slot && resize_slot->resizable && mouse_x >= window->x + window->w - 14 &&
                    mouse_x < window->x + window->w && mouse_y >= window->y + window->h - 14 &&
                    mouse_y < window->y + window->h) {
                    resizing_user = window;
                    window_context_leave(previous);
                    wm_bring_to_front(window);
                    request_redraw();
                    handled = 1;
                    break;
                }
                int close_x = window->x + window->w - 14;
                int close_y = window->y + 11;
                if (terminal_for_window(window) != 0 &&
                    mouse_x >= window->x + window->w - 12 &&
                    mouse_x <= window->x + window->w &&
                    mouse_y >= window->y + window->h - 12 &&
                    mouse_y <= window->y + window->h) {
                    resizing_terminal = window;
                    window_context_leave(previous);
                    wm_bring_to_front(window);
                    request_redraw();
                    handled = 1;
                    break;
                }
                if (mouse_x >= close_x - 8 && mouse_x <= close_x + 8 &&
                    mouse_y >= close_y - 8 && mouse_y <= close_y + 8) {
                    if (terminal_for_window(window)) {
                        terminal_close(window);
                        dragging_window = 0; dragging_directory = 0;
                        resizing_terminal = 0;
                        resizing_user = 0;
                        window_context_leave(previous);
                        request_redraw(); handled = 1; break;
                    }
                    if (notepad_close(window)) {
                        window_context_leave(previous);
                        request_redraw(); handled = 1; break;
                    }
                    uint32_t owner_directory = window_directories[i];
                    if (owner_directory) {
                        user_window_slot_t* user_slot = user_slot_for_window(window);
                        if (user_slot) {
                            user_window_enqueue_simple(user_slot, USER_WINDOW_EVENT_CLOSE);
                            window->visible = 0;
                        } else {
                            wm_unregister_window(window);
                        }
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
                    mouse_y >= window->y && mouse_y <= window->y + 22) {
                    uint32_t owner_directory = window_directories[i];
                    uint32_t caller_directory = previous;
                    window_context_leave(previous);
                    caller_directory = app_loader_context_enter(owner_directory);
                    wm_bring_to_front(window);
                    app_loader_context_leave(caller_directory);
                    dragging_window = window;
                    dragging_directory = owner_directory;
                    request_redraw();
                    handled = 1;
                    break;
                }
                if (mouse_x >= window->x && mouse_x <= window->x + window->w &&
                    mouse_y >= window->y && mouse_y <= window->y + window->h) {
                    uint32_t owner_directory = window_directories[i];
                    uint32_t caller_directory = previous;
                    window_context_leave(previous);
                    caller_directory = app_loader_context_enter(owner_directory);
                    wm_bring_to_front(window);
                    app_loader_context_leave(caller_directory);
                    request_redraw();
                    handled = 1;
                    previous = app_loader_context_enter(owner_directory);
                    user_window_slot_t* user_slot = user_slot_for_window(window);
                    if (user_slot) {
                        user_window_event_t queued = {0};
                        queued.type = USER_WINDOW_EVENT_MOUSE;
                        queued.x = mouse_x - window->x;
                        queued.y = mouse_y - window->y;
                        queued.buttons = left;
                        user_window_enqueue(user_slot, &queued);
                    } else if (window->handle_mouse) {
                        window->handle_mouse(window, mouse_x - window->x,
                                             mouse_y - window->y, left);
                    }
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
    if (volume_open && scancode == 0x01) {
        volume_open = volume_drag = 0;
        request_redraw();
        return;
    }
    if (scancode == 0x58) {
        if (wm_api->graphics.capture_screenshot) wm_api->graphics.capture_screenshot();
        request_redraw();
    }

    /* Feed every byte through modifier/prefix tracking, including special
     * keys whose raw scan code is forwarded to applications. */
    char translated = wm_api->input.translate_scancode ?
        wm_api->input.translate_scancode(scancode) : 0;
    char key = (scancode == 0x3C || scancode == 0x3D || scancode == 0x48 ||
                scancode == 0x50 || scancode == 0x47 || scancode == 0x4F ||
                scancode == 0x49 || scancode == 0x51 || scancode == 0x3F || scancode == 0x01) ? (char)scancode : translated;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        Window* w = windows[i];
        if (!w) continue;
        uint32_t previous = window_context_enter(i);
        if (!w->visible || !w->focused) { window_context_leave(previous); continue; }
        user_window_slot_t* slot = user_slot_for_window(w);
        if (slot && slot->raw_keys) {
            user_window_event_t queued = {0};
            queued.type = USER_WINDOW_EVENT_SCANCODE;
            queued.key = scancode;
            user_window_enqueue(slot, &queued);
            window_context_leave(previous);
            return;
        }
        window_context_leave(previous);
    }
    if (!key) return;
    if (desktop_key(key)) { request_redraw(); return; }
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        Window* window = windows[i];
        if (!window) continue;
        uint32_t previous = window_context_enter(i);
        if (window->visible && window->focused) {
            user_window_slot_t* user_slot = user_slot_for_window(window);
            if (user_slot) {
                user_window_event_t queued = {0};
                queued.type = USER_WINDOW_EVENT_KEY;
                queued.key = (uint8_t)key;
                user_window_enqueue(user_slot, &queued);
            } else if (window->handle_key) {
                window->handle_key(window, key);
            } else {
                window_context_leave(previous);
                continue;
            }
            window_context_leave(previous);
            request_redraw();
            return;
        }
        window_context_leave(previous);
    }
}

uint8_t wm_start_menu_is_open(void) {
    MUTEX_GUARD(kernel_mutex);
    return start_menu_is_open();
}

static void handle_display_resize(void) {
    if (!display_take_resize()) return;
    dragging_window = 0;
    resizing_terminal = 0;
    resizing_user = 0;
    drag_cache.valid = 0;
    start_menu_close();
    start_menu_open = 0;
    mouse_x = mouse_x < SCREEN_WIDTH - 12 ? mouse_x : SCREEN_WIDTH - 12;
    mouse_y = mouse_y < SCREEN_HEIGHT - 19 ? mouse_y : SCREEN_HEIGHT - 19;
    wm_cursor_move(mouse_x, mouse_y, 0);
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!windows[i]) continue;
        uint32_t previous = window_context_enter(i);
        Window* window = windows[i];
        user_window_slot_t* fullscreen_slot = user_slot_for_window(window);
        if (fullscreen_slot && fullscreen_slot->fullscreen) {
            user_window_resize(fullscreen_slot, SCREEN_WIDTH, SCREEN_HEIGHT);
            window->x = window->y = 0;
            window_context_leave(previous);
            continue;
        }
        /* Keep title bars reachable without changing app-owned surface sizes. */
        if (window->x + window->w > SCREEN_WIDTH) window->x = SCREEN_WIDTH - window->w;
        if (window->y + window->h > WM_TASKBAR_Y) window->y = WM_TASKBAR_Y - window->h;
        if (window->x < 0) window->x = 0;
        if (window->y < 0) window->y = 0;
        user_window_slot_t* slot = user_slot_for_window(window);
        if (slot) user_window_enqueue_simple(slot, USER_WINDOW_EVENT_REDRAW);
        window_context_leave(previous);
    }
    desktop_resize();
    request_redraw();
}

void wm_poll(void) {
    startup_trace_poll(1);
    uint32_t clock_flags = task_irq_save();
    uint64_t requested = scheduler_clock();
    task_irq_restore(clock_flags);
    mutex_lock(&kernel_mutex);
    if (!wm_api) { mutex_unlock(&kernel_mutex); startup_trace_poll(0); return; }
    clock_flags = task_irq_save();
    uint64_t acquired = scheduler_clock();
    task_irq_restore(clock_flags);
    handle_display_resize();
    settings_poll();
    uint32_t keys = 0, mouse = 0;

    static uint8_t last_min = 0xFF;
    static uint32_t last_check = 0;

    /* check time every 100 ticks (= 1second) */
    if (wm_api->network.get_ticks) {
        uint32_t ticks = wm_api->network.get_ticks();
        if ((uint32_t)(ticks - last_check) >= 100u) {
            last_check = ticks;
            rtc_time_t t;
            startup_trace_event(TR_PHASE_IN,TR_PHASE_RTC,0,0);
            rtc_get_time(&t);
            startup_trace_event(TR_PHASE_OUT,TR_PHASE_RTC,0,0);
            if (t.minute != last_min) {
                last_min = t.minute;
                redraw_pending = 1;
            }
        }
    }


    desktop_poll();
    os_input_event_t event;
    event.type = OS_INPUT_NONE;
    for (unsigned budget = 16; budget; budget--) {
        startup_trace_event(TR_PHASE_IN,TR_PHASE_INPUT,0,0);
        int available=wm_api->input.poll_input && wm_api->input.poll_input(&event);
        startup_trace_event(TR_PHASE_OUT,TR_PHASE_INPUT,available,0);
        if (!available) break;
        startup_trace_event(TR_PHASE_IN,TR_PHASE_EVENT,event.type,0);
        if (event.type == OS_INPUT_MOUSE) { mouse++; handle_mouse_event(&event); }
        else if (event.type == OS_INPUT_KEY) { keys++; handle_key_event(&event); }
        startup_trace_event(TR_PHASE_OUT,TR_PHASE_EVENT,event.type,0);
    }
    if (wm_api->graphics.take_redraw_request && wm_api->graphics.take_redraw_request()) {
        scene_generation++;
        redraw_pending = 1;
    }
    static uint64_t next_frame;
    uint32_t irq_flags = task_irq_save();
    uint64_t now = scheduler_clock();
    task_irq_restore(irq_flags);
    if (redraw_pending && now >= next_frame && wm_api->graphics.render_screen) {
        if (!next_frame || now - next_frame > 16666667u) next_frame = now + 16666667u;
        else next_frame += 16666667u;
        startup_trace_event(TR_PHASE_IN,TR_PHASE_RENDER,0,0);
        wm_api->graphics.render_screen();
        startup_trace_event(TR_PHASE_OUT,TR_PHASE_RENDER,0,0);
        redraw_pending = 0;
    }
    clock_flags = task_irq_save();
    uint64_t finished = scheduler_clock();
    task_irq_restore(clock_flags);
    mutex_unlock(&kernel_mutex);
    startup_trace_poll(0);
    debug_input_progress(requested, acquired, finished, keys, mouse);
}

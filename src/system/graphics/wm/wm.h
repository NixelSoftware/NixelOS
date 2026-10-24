#ifndef SYSTEM_GRAPHICS_WM_H
#define SYSTEM_GRAPHICS_WM_H

#include "../../../kernel/os_api.h"
#include "../../user/window_api.h"

#define WM_MAX_WINDOWS 16
#define WM_TASKBAR_Y 568
#define WM_TASKBAR_HEIGHT 32

void wm_initialize(os_api_t* api, void (*invalidate)(void));
Window* wm_get_window(int index);
int wm_register_window(Window* window);
void wm_unregister_window(Window* window);
void wm_bring_to_front(Window* window);
void wm_draw_windows(void);
void wm_draw_taskbar(uint8_t start_menu_open);
void wm_poll(void);
void wm_invalidate(void);
uint8_t wm_start_menu_is_open(void);
typedef struct {
    uint32_t frames, over_budget;
    uint64_t total_ns, max_ns;
} wm_render_stats_t;
/* Low-overhead counters for diagnosing renderer latency in a debugger. */
extern wm_render_stats_t wm_render_stats;
void wm_render_screen(void);
uint8_t wm_render_drag(void);
int32_t wm_user_window_create(const user_window_desc_t* description,
                              uint32_t owner_directory);
int32_t wm_user_window_draw(uint32_t handle, uint32_t owner_directory,
                            const user_window_draw_t* drawing,
                            const char* text, const uint8_t* image_data);
int32_t wm_user_window_poll(uint32_t handle, uint32_t owner_directory,
                            user_window_event_t* event);
int32_t wm_user_window_present(uint32_t handle, uint32_t owner_directory);
int32_t wm_user_window_close(uint32_t handle, uint32_t owner_directory);
void wm_user_close_address_space(uint32_t owner_directory);
void request_redraw(void);
void toggle_window(const char* title);

#endif

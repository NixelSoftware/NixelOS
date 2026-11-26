#include "../../../kernel/mutex.h"
#include "../../../kernel/drivers/display.h"
#include "../../../kernel/drivers/display/font.h"
#include "cursor.h"
#include "desktop.h"
#include "desktop_wallpaper.h"
#include "wm.h"
#include "../../../kernel/drivers/serial.h"

wm_render_stats_t wm_render_stats;
static uint64_t last_render_log;

void wm_render_screen(void) {
    uint32_t flags = task_irq_save();
    uint64_t requested = scheduler_clock();
    task_irq_restore(flags);
    mutex_lock(&kernel_mutex);
    /* A callback can request a nested full-scene repaint. Each window will
     * install its own content bounds; desktop and taskbar use the full screen. */
    font_text_scope_t text_scope = font_text_scope_begin(
        (text_rect_t){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT});
    flags = task_irq_save();
    uint64_t started = scheduler_clock();
    task_irq_restore(flags);
    /* The drag path keeps its own cache and always paints unclipped, but it
     * still consumes the pending damage. */
    text_rect_t damage = wm_take_damage();
    text_rect_t outer_clip = display_set_clip(
        (text_rect_t){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT});
    if (!wm_render_drag()) {
        display_set_clip(damage);
        //display_clear(COLOR_DESKTOP);
        desktop_wallpaper_draw();
        desktop_draw();
        wm_draw_windows();
        wm_draw_taskbar(wm_start_menu_is_open());
        desktop_draw_overlay();
        display_blit_rect(damage.left, damage.top,
                          damage.right - damage.left, damage.bottom - damage.top);
        wm_cursor_draw();
    }
    display_set_clip(outer_clip);
    flags = task_irq_save();
    uint64_t finished = scheduler_clock();
    uint64_t elapsed = finished - started;
    task_irq_restore(flags);
    wm_render_stats.total_ns += elapsed;
    if (elapsed > wm_render_stats.max_ns) wm_render_stats.max_ns = elapsed;
    if (elapsed > 16666667u) wm_render_stats.over_budget++;
    wm_render_stats.frames++;
    font_text_scope_end(text_scope);
    mutex_unlock(&kernel_mutex);
    debug_frame_timing("render", requested, started, finished, &last_render_log);
}

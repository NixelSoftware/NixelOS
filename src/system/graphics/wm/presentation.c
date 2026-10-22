#include "../../../kernel/mutex.h"
#include "../../../kernel/drivers/display.h"
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
    flags = task_irq_save();
    uint64_t started = scheduler_clock();
    task_irq_restore(flags);
    if (!wm_render_drag()) {
        //display_clear(COLOR_DESKTOP);
        desktop_wallpaper_draw();
        desktop_draw();
        wm_draw_windows();
        wm_draw_taskbar(wm_start_menu_is_open());
        display_blit();
        wm_cursor_draw();
    }
    flags = task_irq_save();
    uint64_t finished = scheduler_clock();
    uint64_t elapsed = finished - started;
    task_irq_restore(flags);
    wm_render_stats.total_ns += elapsed;
    if (elapsed > wm_render_stats.max_ns) wm_render_stats.max_ns = elapsed;
    if (elapsed > 16666667u) wm_render_stats.over_budget++;
    wm_render_stats.frames++;
    mutex_unlock(&kernel_mutex);
    debug_frame_timing("render", requested, started, finished, &last_render_log);
}

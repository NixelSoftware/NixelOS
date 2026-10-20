#include "../../../kernel/drivers/display.h"
#include "cursor.h"
#include "desktop.h"
#include "wm.h"
#include "../../../kernel/drivers/serial.h"

void wm_render_screen(void) {
    desktop_trace_api("before render display_clear");
    debug_subsystem_print("[RENDER TRACE] before display_clear\n");
    display_clear(COLOR_DESKTOP);
    debug_subsystem_print("[RENDER TRACE] after display_clear\n");
    desktop_trace_api("after render display_clear");
    desktop_draw();
    debug_subsystem_print("[RENDER TRACE] after desktop_draw\n");
    wm_draw_windows();
    debug_subsystem_print("[RENDER TRACE] after wm_draw_windows\n");
    wm_draw_taskbar(wm_start_menu_is_open());
    debug_subsystem_print("[RENDER TRACE] after taskbar\n");
    //wm_cursor_draw();
    debug_subsystem_print("[RENDER TRACE] before display_blit\n");
    display_blit();
    debug_subsystem_print("[RENDER TRACE] after display_blit\n");
    wm_cursor_draw();
    debug_subsystem_print("[RENDER TRACE] after cursor draw\n");
}

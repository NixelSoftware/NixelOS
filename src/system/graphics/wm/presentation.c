#include "../../../kernel/drivers/display.h"
#include "cursor.h"
#include "desktop.h"
#include "wm.h"

void wm_render_screen(void) {
    display_clear(COLOR_DESKTOP);
    desktop_draw();
    wm_draw_windows();
    wm_draw_taskbar(wm_start_menu_is_open());
    //wm_cursor_draw();
    display_blit();
    wm_cursor_draw();
}

#include "session.h"

#include "../../apps/notepad/notepad.h"
#include "../../apps/viewer/viewer.h"
#include "../shell/commands.h"
#include "terminal/terminal.h"
#include "dialog/dialog.h"
#include "wm/desktop.h"
#include "wm/wm.h"
#include "../../kernel/drivers/serial.h"

void system_graphics_initialize(os_api_t* api, system_redraw_fn request_redraw) {
    if (!api) return;

    debug_print("[APPS] shell init...\n");
    shell_initialize(api);
    debug_print("[APPS] shell init done\n");
    debug_print("[APPS] terminal init...\n");
    terminal_init(api);
    debug_print("[APPS] terminal init done\n");
    debug_print("[APPS] notepad init...\n");
    notepad_init();
    debug_print("[APPS] notepad init done\n");
    debug_print("[APPS] viewer init...\n");
    viewer_init();
    debug_print("[APPS] viewer init done\n");
    debug_print("[APPS] dialog init...\n");
    dialog_init(api);

    debug_print("[APPS] window manager init...\n");
    wm_initialize(api, request_redraw);
    debug_print("[APPS] register notepad...\n");
    wm_register_window(&notepad_window);
    debug_print("[APPS] register viewer...\n");
    wm_register_window(&viewer_window);
    debug_print("[APPS] register terminal...\n");
    wm_register_window(&terminal_window);
    debug_print("[APPS] desktop init...\n");
    desktop_init(api, &terminal_window, &notepad_window);
    debug_print("[APPS] initialization done\n");
}

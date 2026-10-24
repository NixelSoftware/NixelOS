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
    desktop_init(api);
    /* One initial shell; each icon activation creates an additional instance. */
    if (terminal_launch() < 0) debug_print("[APPS] Cannot create initial terminal\n");
    debug_print("[APPS] initialization done\n");
}

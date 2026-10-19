#include "session.h"

#include "../../apps/notepad/notepad.h"
#include "../../apps/viewer/viewer.h"
#include "../shell/commands.h"
#include "terminal/terminal.h"
#include "dialog/dialog.h"
#include "wm/desktop.h"
#include "wm/wm.h"

void system_graphics_initialize(os_api_t* api, system_redraw_fn request_redraw) {
    if (!api) return;

    shell_initialize(api);
    terminal_init(api);
    notepad_init();
    viewer_init();
    dialog_init(api);

    wm_initialize(api, request_redraw);
    wm_register_window(&notepad_window);
    wm_register_window(&viewer_window);
    wm_register_window(&terminal_window);
    desktop_init(api, &terminal_window, &notepad_window);
}

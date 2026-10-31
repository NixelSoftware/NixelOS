#include "../../system/graphics/wm/theme.h"
#include "../../system/user/sys_api.h"

static uint32_t window;
static const char* status = "Press Enter or click Finish Setup.";
static void draw(uint32_t op, int x, int y, int w, int h, const char* text, uint32_t color) {
    user_window_draw_t d = {0};
    d.operation = op; d.x = x; d.y = y; d.width = w; d.height = h;
    d.text = text; d.color = color;
    sys_window_draw(window, &d);
}
static void redraw(void) {
    draw(USER_WINDOW_DRAW_FILL, 0, 0, 600, 300, 0, COLOR_SURFACE);
    draw(USER_WINDOW_DRAW_TEXT, 28, 40, 0, 0, "Welcome to your new NixelOS installation!", COLOR_TEXT_PRIMARY);
    draw(USER_WINDOW_DRAW_TEXT, 28, 90, 0, 0, "Setup is ready to finish and restart the computer.", COLOR_TEXT_PRIMARY);
    draw(USER_WINDOW_DRAW_TEXT, 28, 140, 0, 0, status, COLOR_TEXT_PRIMARY);
    draw(USER_WINDOW_DRAW_BUTTON, 400, 230, 160, 36, "Finish Setup", COLOR_SURFACE);
    sys_window_present(window);
}
static void finish_setup(void) {
    const char marker = '1';
    if (sys_fs_write("/var/lib/nixel/oobe.done", &marker, 1) < 0) {
        status = "Cannot save setup state.";
    } else {
        int32_t result = sys_reboot();
        status = result == SYS_ERR_PERMISSION ?
            "Restart requires root. Run sudo reboot when ready." : "Restart failed.";
    }
    redraw();
}
void user_main(const char* arguments) {
    (void)arguments;
    user_window_desc_t desc = {100, 100, 600, 300, "NixelOS Setup"};
    int32_t handle = sys_window_create(&desc);
    if (handle < 0) return;
    window = (uint32_t)handle;
    redraw();
    int closed = 0;
    while (!closed) {
        user_window_event_t event;
        while (sys_window_poll(window, &event) == 1) {
            if (event.type == USER_WINDOW_EVENT_CLOSE) closed = 1;
            else if (event.type == USER_WINDOW_EVENT_REDRAW) redraw();
            else if (event.type == USER_WINDOW_EVENT_KEY) {
                if (event.key == 27 || event.key == 1) closed = 1;
                else if (event.key == '\n' || event.key == '\r' || event.key == ' ') finish_setup();
            } else if (event.type == USER_WINDOW_EVENT_MOUSE && (event.buttons & 1) &&
                       event.x >= 400 && event.x < 560 && event.y >= 230 && event.y < 266)
                finish_setup();
            if (closed) break;
        }
        if (!closed) sys_yield();
    }
    sys_window_close(window);
}

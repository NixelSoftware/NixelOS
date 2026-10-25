#include "../engine/platform/nixel.h"
#include "../engine/platform/netzhaut/Probe.h"

/* OS window/input integration only. Page processing belongs to Netzhaut.
 * The imported DOM/HTML runtime is not linkable yet; no native engine fallback.
 */
static void draw_text(uint32_t handle, int y, const char *text) {
    user_window_draw_t draw = {0};
    draw.operation = USER_WINDOW_DRAW_TEXT;
    draw.x = 12;
    draw.y = y;
    draw.text = text;
    draw.color = 0;
    (void)sys_window_draw(handle, &draw);
}
static void redraw(uint32_t handle, int core_ok, int dom_ok) {
    user_window_draw_t draw = {0};
    draw.operation = USER_WINDOW_DRAW_FILL;
    draw.width = 480;
    draw.height = 360;
    draw.color = 0xffffff;
    (void)sys_window_draw(handle, &draw);
    draw_text(handle, 20, "Netzhaut port in progress");
    draw_text(handle, 40, core_ok ? "Original core/UTF-8/UTF-32 check passed." :
                                  "Original core/encoding check failed.");
    draw_text(handle, 68, dom_ok ? "Original WebIDL/DOM checks passed." :
                                 "WebIDL/DOM checks failed; see console.");
    draw_text(handle, 96, "Page loading and rendering are unavailable.");
    draw_text(handle, 124, "Original HTML/layout integration is pending.");
    (void)sys_window_present(handle);
}
void user_main(const char *arguments) {
    (void)arguments;
    int core_ok = nh_nixel_probe();
    int dom_ok = core_ok && nh_nixel_probe_dom();
    user_window_desc_t desc = {0};
    desc.x = 80; desc.y = 40; desc.width = 480; desc.height = 360;
    const char title[] = "Nixplorer - Netzhaut port";
    for (unsigned i = 0; i < sizeof(title) && i < sizeof(desc.title); ++i)
        desc.title[i] = title[i];
    int32_t created = sys_window_create(&desc);
    if (created < 0) return;
    uint32_t handle = (uint32_t)created;
    redraw(handle, core_ok, dom_ok);
    for (;;) {
        user_window_event_t event = {0};
        sys_yield();
        int32_t available = sys_window_poll(handle, &event);
        if (available < 0 || (available && event.type == USER_WINDOW_EVENT_CLOSE)) break;
        if (available && event.type == USER_WINDOW_EVENT_REDRAW) redraw(handle, core_ok, dom_ok);
    }
    (void)sys_window_close(handle);
}

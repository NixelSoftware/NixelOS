#include "../../system/graphics/wm/theme.h"
#include "../../system/user/sys_api.h"

typedef struct { const char* name; const char* result; } test_t;
static test_t tests[] = {
    {"ext4 write/read/delete", "Pending"}, {"Network controller", "Pending"},
    {"Raw floppy read", "Pending (requires sudo)"}, {"Window drawing", "Pending"},
    {"Display colors", "Pending"}, {"Keyboard input", "Pending"}, {"Mouse input", "Pending"}
};
static const uint32_t colors[] = {0xff0000, 0x00ff00, 0x0000ff, 0xffffff,
                                 0x000000, 0xffff00, 0x00ffff, 0xff00ff};
static uint32_t window;
static int color_step = -1, input_step = -1;
static void draw(uint32_t op, int x, int y, int w, int h, const char* text, uint32_t color) {
    user_window_draw_t d = {0};
    d.operation = op; d.x = x; d.y = y; d.width = w; d.height = h;
    d.text = text; d.color = color;
    sys_window_draw(window, &d);
}
static void redraw(void) {
    draw(USER_WINDOW_DRAW_FILL, 0, 0, 740, 500, 0, COLOR_SURFACE);
    if (color_step >= 0) {
        draw(USER_WINDOW_DRAW_FILL, 0, 0, 740, 500, 0, colors[color_step]);
        draw(USER_WINDOW_DRAW_FILL, 15, 25, 620, 44, 0, COLOR_SURFACE);
        draw(USER_WINDOW_DRAW_TEXT, 25, 40, 0, 0, "Color test: P = next/pass, F = fail, Esc = exit", COLOR_TEXT_PRIMARY);
    } else {
        for (unsigned i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
            draw(USER_WINDOW_DRAW_TEXT, 20, 40 + i*52, 0, 0, tests[i].name, COLOR_TEXT_PRIMARY);
            draw(USER_WINDOW_DRAW_TEXT, 250, 40 + i*52, 0, 0, tests[i].result, COLOR_TEXT_PRIMARY);
        }
        const char* prompt = input_step == 5 ? "Keyboard: press P to pass or F to fail." :
            input_step == 6 ? "Mouse: click here to pass or press F to fail." :
            "Enter runs tests. Escape closes this window.";
        draw(USER_WINDOW_DRAW_TEXT, 20, 410, 0, 0, prompt, COLOR_ACCENT);
        draw(USER_WINDOW_DRAW_BUTTON, 20, 450, 140, 30, "Run Tests", COLOR_SURFACE);
        draw(USER_WINDOW_DRAW_BUTTON, 180, 450, 120, 30, "Exit", COLOR_SURFACE);
    }
    sys_window_present(window);
}
static void storage_test(void) {
    /* Keep tests in the user's configuration directory, with a distinct file
     * for each concurrent diagnostic process. */
    char directory[FS_MAX_PATH], path[FS_MAX_PATH];
    int n = sys_fs_home(directory, sizeof(directory));
    if (n < 0 || n > FS_MAX_PATH - 40) { tests[0].result = "FAIL: home unavailable"; return; }
    const char* prefix = "/.config/sysdiag";
    while (*prefix) directory[n++] = *prefix++;
    directory[n] = 0;
    if (sys_fs_mkdir(directory) < 0) { tests[0].result = "FAIL: cannot create test directory"; return; }
    for (int i = 0; i <= n; i++) path[i] = directory[i];
    path[n++] = '/';
    uint32_t pid = (uint32_t)sys_getpid();
    for (int shift = 28; shift >= 0; shift -= 4) path[n++] = "0123456789abcdef"[(pid >> shift)&15];
    path[n] = 0;
    static const char pattern[] = "NIXELOS_STORAGE_VERIFY_PATTERN_12345678";
    char readback[sizeof(pattern)];
    int ok = sys_fs_write(path, pattern, sizeof(pattern)) >= 0 &&
        sys_fs_read(path, readback, sizeof(readback)) == sizeof(pattern);
    for (unsigned i = 0; ok && i < sizeof(pattern); i++) ok = readback[i] == pattern[i];
    int removed = sys_fs_delete(path);
    tests[0].result = ok && removed == 0 ? "PASS" : "FAIL: storage operation";
}
static void run_tests(void) {
    storage_test();
    net_diagnostics_t network;
    int ready = sys_net_diagnostics(&network);
    tests[1].result = ready >= 0 && network.initialized ? "PASS: controller initialized" : "FAIL: no initialized controller";
    int floppy = sys_floppy_init();
    uint8_t sector[512];
    tests[2].result = floppy == SYS_ERR_PERMISSION ? "Requires sudo sysdiag" :
        floppy == 1 && sys_floppy_read(0, 1, sector) == 1 ? "PASS" : "FAIL: drive unavailable";
    user_window_draw_t d = {0};
    d.operation = USER_WINDOW_DRAW_FILL; d.x = 700; d.y = 400;
    d.width = d.height = 10; d.color = 0xff0000;
    tests[3].result = sys_window_draw(window, &d) == 0 ? "PASS: drawing request accepted" : "FAIL: drawing request";
    tests[4].result = "In progress";
    tests[5].result = tests[6].result = "Pending";
    input_step = -1; color_step = 0;
    redraw();
}
static void key(uint32_t c) {
    if (color_step >= 0) {
        if (c == 'f' || c == 'F') {
            tests[4].result = "FAIL: user assessment"; color_step = -1;
        } else if ((c == 'p' || c == 'P') && ++color_step == 8) {
            tests[4].result = "PASS: user assessment"; color_step = -1;
        }
        if (color_step < 0) { input_step = 5; tests[5].result = "Press P or F"; }
    } else if (input_step >= 0 && (c == 'p' || c == 'P' || c == 'f' || c == 'F')) {
        /* Mouse passes only on a mouse event, never a keyboard confirmation. */
        if (input_step == 5 || c == 'f' || c == 'F') {
            tests[input_step].result = c == 'f' || c == 'F' ? "FAIL: user assessment" : "PASS";
            if (++input_step > 6) input_step = -1;
            else tests[6].result = "Click prompt to pass";
        }
    } else if (c == '\n' || c == '\r') run_tests();
    redraw();
}
void user_main(const char* arguments) {
    (void)arguments;
    user_window_desc_t desc = {30, 40, 740, 500, "System Diagnostics"};
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
                else key(event.key);
            } else if (event.type == USER_WINDOW_EVENT_MOUSE && (event.buttons & 1) && color_step < 0) {
                if (input_step == 6 && event.y >= 400 && event.y < 440) {
                    tests[6].result = "PASS: click received"; input_step = -1; redraw();
                } else if (event.y >= 450 && event.y < 480) {
                    if (event.x >= 20 && event.x < 160) run_tests();
                    else if (event.x >= 180 && event.x < 300) closed = 1;
                }
            }
            if (closed) break;
        }
        if (!closed) sys_yield();
    }
    sys_window_close(window);
}

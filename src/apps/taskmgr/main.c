#include "../../system/graphics/wm/theme.h"
#include "../../system/user/sys_api.h"

#define WIN_W 420
#define WIN_H 360
#define MAX_PROC 32

static uint32_t window;
static user_task_info_t procs[MAX_PROC];
static int proc_count = 0;
static int selected_index = -1;
static user_memory_info_t mem_info;

static void format_num(char* buf, uint32_t val) {
    char tmp[12];
    int n = 0;
    if (!val) { buf[0] = '0'; buf[1] = 0; return; }
    while (val) { tmp[n++] = '0' + (val % 10); val /= 10; }
    int i = 0;
    while (n) buf[i++] = tmp[--n];
    buf[i] = 0;
}

static void draw(uint32_t op, int x, int y, int w, int h, const char* text, uint32_t color) {
    user_window_draw_t d = {0};
    d.operation = op; d.x = x; d.y = y; d.width = w; d.height = h;
    d.text = text; d.color = color;
    sys_window_draw(window, &d);
}

static void refresh_data(void) {
    proc_count = sys_task_list(procs, MAX_PROC);
    sys_memory_info(&mem_info);
}

static void render(void) {
    draw(USER_WINDOW_DRAW_FILL, 0, 0, WIN_W, WIN_H, 0, COLOR_SURFACE);

    /* Memory Info Header */
    draw(USER_WINDOW_DRAW_FILL, 8, 24, WIN_W - 16, 32, 0, COLOR_MODERN_BG);
    draw(USER_WINDOW_DRAW_TEXT, 16, 30, 0, 0, "RAM Used:", COLOR_TEXT_MUTED);

    char mem_buf[32];
    uint32_t used_mib = mem_info.used_bytes / (1024 * 1024);
    uint32_t total_mib = mem_info.total_bytes / (1024 * 1024);
    format_num(mem_buf, used_mib);
    int p = 0; while (mem_buf[p]) p++;
    mem_buf[p++] = ' '; mem_buf[p++] = '/'; mem_buf[p++] = ' ';
    format_num(mem_buf + p, total_mib);
    while (mem_buf[p]) p++;
    mem_buf[p++] = ' '; mem_buf[p++] = 'M'; mem_buf[p++] = 'i'; mem_buf[p++] = 'B'; mem_buf[p] = 0;
    draw(USER_WINDOW_DRAW_TEXT, 90, 30, 0, 0, mem_buf, COLOR_TEXT_PRIMARY);

    /* Process Table Header */
    int top_y = 64;
    draw(USER_WINDOW_DRAW_FILL, 8, top_y, WIN_W - 16, 18, 0, COLOR_TASKBAR_ITEM_ACT);
    draw(USER_WINDOW_DRAW_TEXT, 16, top_y + 4, 0, 0, "PID", COLOR_ACCENT);
    draw(USER_WINDOW_DRAW_TEXT, 64, top_y + 4, 0, 0, "PPID", COLOR_TEXT_MUTED);
    draw(USER_WINDOW_DRAW_TEXT, 120, top_y + 4, 0, 0, "NAME", COLOR_TEXT_PRIMARY);

    /* Process List */
    int item_y = top_y + 20;
    for (int i = 0; i < proc_count && i < 11; i++) {
        uint32_t bg = (i == selected_index) ? COLOR_TASKBAR_ITEM_ACT : (i % 2 ? COLOR_MODERN_BG : COLOR_SURFACE);
        draw(USER_WINDOW_DRAW_FILL, 8, item_y, WIN_W - 16, 18, 0, bg);

        char pid_str[12], ppid_str[12];
        format_num(pid_str, procs[i].pid);
        format_num(ppid_str, procs[i].ppid);

        draw(USER_WINDOW_DRAW_TEXT, 16, item_y + 4, 0, 0, pid_str, (i == selected_index) ? COLOR_ACCENT : COLOR_TEXT_PRIMARY);
        draw(USER_WINDOW_DRAW_TEXT, 64, item_y + 4, 0, 0, ppid_str, COLOR_TEXT_MUTED);
        draw(USER_WINDOW_DRAW_TEXT, 120, item_y + 4, 0, 0, procs[i].name, COLOR_TEXT_PRIMARY);

        item_y += 20;
    }

    /* Bottom Button: End Task */
    draw(USER_WINDOW_DRAW_BUTTON, WIN_W - 100, WIN_H - 32, 90, 24, "End Task", 0);
    sys_window_present(window);
}

void user_main(const char* args) {
    (void)args;
    user_window_desc_t desc = {
        .x = 120, .y = 80,
        .width = WIN_W, .height = WIN_H,
        .title = "Task Manager"
    };

    int32_t handle = sys_window_create(&desc);
    if (handle < 0) sys_exit(1);
    window = (uint32_t)handle;

    refresh_data();
    render();

    uint32_t last_tick = sys_get_ticks();

    for (;;) {
        user_window_event_t event;
        while (sys_window_poll(window, &event)) {
            if (event.type == USER_WINDOW_EVENT_CLOSE) {
                sys_window_close(window);
                sys_exit(0);
            }
            if (event.type == USER_WINDOW_EVENT_MOUSE) {
                if (event.buttons & 1) {
                    /* Check process row selection */
                    if (event.y >= 84 && event.y < 84 + (proc_count * 20)) {
                        selected_index = (event.y - 84) / 20;
                        render();
                    }
                    /* Check "End Task" button */
                    if (event.x >= WIN_W - 100 && event.x <= WIN_W - 10 &&
                        event.y >= WIN_H - 32 && event.y <= WIN_H - 8) {
                        if (selected_index >= 0 && selected_index < proc_count) {
                            sys_task_kill(procs[selected_index].pid);
                            refresh_data();
                            selected_index = -1;
                            render();
                        }
                    }
                }
            }
        }

        /* Auto-refresh list and memory every 1 second (100 ticks) */
        uint32_t now = sys_get_ticks();
        if (now - last_tick >= 100) {
            last_tick = now;
            refresh_data();
            render();
        }

        sys_yield();
    }
}
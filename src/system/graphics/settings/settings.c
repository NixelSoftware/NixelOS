#include "settings.h"
#include "../wm/wm.h"
#include "../wm/theme.h"
#include "../../../kernel/drivers/display.h"
#include "../../../kernel/drivers/display/font.h"
#include "../../../kernel/hal/video.h"
#include "../../../kernel/hal/audio.h"
#include "../../../kernel/hal/net.h"
#include "../../../kernel/task.h"
#include "../../../kernel/mutex.h"

static Window window;
static int tab, mode, busy, network_action;
static char manual[3][16];
static int field = -1, replace_field;
static uint32_t saved_volume = 75;
static const char* message = "Nothing to report.";
static uint64_t message_expires;
static void set_message(const char* text) {
    message = text;
    message_expires = scheduler_clock() + 3000000000ull;
}
static const uint32_t widths[] = {640, 800, 1024, 1280, 1920};
static const uint32_t heights[] = {480, 600, 768, 720, 1080};
static const char* modes[] = {"640 x 480", "800 x 600", "1024 x 768", "1280 x 720", "1920 x 1080"};

static void text(int x, int y, const char* s, uint32_t color) {
    draw_string(window.x + x, window.y + y, s, color);
}
static void button(int x, int y, int w, const char* s, int selected) {
    display_fill_rect(window.x + x, window.y + y, w, 28,
                      selected ? COLOR_ACCENT : COLOR_SURFACE_BORDER);
    text(x + 8, y + 7, s, selected ? COLOR_MODERN_BG : COLOR_TEXT_PRIMARY);
}
static char* number(char* out, uint32_t value) {
    char digits[10]; int n = 0;
    do { digits[n++] = '0' + value % 10; value /= 10; } while (value);
    while (n) *out++ = digits[--n];
    *out = 0; return out;
}
static void format_ip(char* buf, uint32_t ip) {
    char* p = buf;
    for (int i = 3; i >= 0; i--) {
        p = number(p, (ip >> (i * 8)) & 255);
        if (i) *p++ = '.';
    }
    *p = 0;
}
static void load_manual(void) {
    uint32_t mask, gateway;
    sys_net_get_configuration(&mask, &gateway, 0);
    format_ip(manual[0], sys_net_get_ip_address());
    format_ip(manual[1], mask); format_ip(manual[2], gateway);
    field = -1;
}
static int parse_ip(const char* s, uint32_t* out) {
    uint32_t ip = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t value = 0; int digits = 0;
        while (*s >= '0' && *s <= '9') {
            value = value * 10 + *s++ - '0';
            if (++digits > 3 || value > 255) return 0;
        }
        if (!digits || (i < 3 ? *s++ != '.' : *s != 0)) return 0;
        ip = (ip << 8) | value;
    }
    *out = ip; return 1;
}
static void apply_manual(void) {
    uint32_t ip, mask, gateway;
    if (busy) return;
    if (!parse_ip(manual[0], &ip) || !parse_ip(manual[1], &mask) ||
        !parse_ip(manual[2], &gateway)) {
        set_message("Enter valid IPv4 values (gateway: 0.0.0.0 if none).");
    } else if (!sys_net_set_manual_configuration(ip, mask, gateway)) {
        set_message("Check adapter, address, mask and gateway subnet.");
    } else { set_message("Manual network configuration applied."); field = -1; }
    request_redraw();
}
static void key(Window* win, char c) {
    (void)win;
    if (tab != 1 || busy) return;
    if (c == '\t') { field = (field + 1) % 3; replace_field = 1; }
    else if (c == '\n' || c == '\r') apply_manual();
    else if (field >= 0) {
        int n = 0; while (manual[field][n]) n++;
        if (c == '\b' || c == 127) {
            if (replace_field) n = 0;
            else if (n) n--;
            manual[field][n] = 0; replace_field = 0;
        } else if ((c >= '0' && c <= '9') || c == '.') {
            if (replace_field) n = 0;
            replace_field = 0;
            if (n < 15) { manual[field][n++] = c; manual[field][n] = 0; }
        }
    }
    request_redraw();
}
static void draw(Window* win) {
    (void)win;
    display_fill_rect(window.x + 2, window.y + 23, window.w - 4, window.h - 25, COLOR_SURFACE);
    button(16, 36, 140, "Display", tab == 0);
    button(168, 36, 140, "Network", tab == 1);
    button(320, 36, 140, "Sound", tab == 2);
    if (tab == 0) {
        char buf[40], *p = number(buf, SCREEN_WIDTH); *p++ = 'x'; number(p, SCREEN_HEIGHT);
        text(20, 88, "Current resolution", COLOR_TEXT_MUTED); text(210, 88, buf, COLOR_TEXT_PRIMARY);
        const video_framebuffer_t* fb = video_get_framebuffer();
        text(20, 116, fb ? fb->driver_name : "No display driver", COLOR_TEXT_MUTED);
        text(20, 156, "Choose resolution", COLOR_TEXT_PRIMARY);
        button(20, 188, 40, "<", 0); button(72, 188, 220, modes[mode], 0);
        button(304, 188, 40, ">", 0); button(356, 188, 104, "Apply", 0);
        int state = video_resolution_status();
        text(20, 244, state == 1 ? "Applying resolution..." : state == -1 ?
             "This display could not apply the mode." : state == 2 ? "Resolution applied." :
             video_can_set_resolution() ? "Choose a mode and click Apply." :
             "Firmware mode: resolution switching unavailable.", COLOR_TEXT_MUTED);
    } else if (tab == 1) {
        const char* name = 0; sys_net_get_device_info(0, 0, &name);
        text(20, 88, name ? name : "No network adapter", COLOR_TEXT_PRIMARY);
        text(20, 116, sys_net_is_ready() ? "Adapter enabled" : "Adapter disabled / unavailable", COLOR_TEXT_MUTED);
        uint32_t mask, gateway; uint8_t leased;
        sys_net_get_configuration(&mask, &gateway, &leased);
        const char* labels[] = {"IP address", "Subnet mask", "Gateway"};
        for (int i = 0; i < 3; i++) {
            int y = 144 + i * 36;
            text(20, y + 7, labels[i], COLOR_TEXT_MUTED);
            display_fill_rect(window.x + 158, window.y + y, 302, 28,
                              field == i ? COLOR_SURFACE_BORDER : COLOR_MODERN_BG);
            text(166, y + 7, manual[i], field == i ? COLOR_ACCENT : COLOR_TEXT_PRIMARY);
        }
        text(20, 254, leased ? "DHCP lease acquired" : sys_net_get_ip_address() ?
             "Manual configuration" : "No address: renew DHCP or enter values above.", COLOR_TEXT_MUTED);
        button(20, 276, 140, "Enable", 0); button(172, 276, 140, "Disable", 0);
        button(324, 276, 136, "Renew DHCP", 0);
        button(20, 310, 200, "Apply manual settings", 0);
    } else {
        uint32_t count = audio_device_count(), device = audio_device_selected();
        text(20, 88, "Output device", COLOR_TEXT_MUTED);
        const char* name = count ? audio_device_name(device) : 0;
        text(20, 116, name ? name : "No audio device", COLOR_TEXT_PRIMARY);
        button(20, 152, 220, "Next output device", 0);
        char buf[16]; char* p = number(buf, audio_get_volume()); *p++ = '%'; *p = 0;
        text(20, 208, "Master volume", COLOR_TEXT_MUTED); text(200, 208, buf, COLOR_TEXT_PRIMARY);
        button(20, 244, 64, "-", 0); button(96, 244, 64, "+", 0);
        button(172, 244, 140, audio_get_volume() ? "Mute" : "Unmute", 0);
    }
    text(20, 352, busy ? "Updating network..." : message, COLOR_TEXT_MUTED);
}
static void network_worker(void* arg) {
    (void)arg;
    if (network_action == 1) {
        if (!sys_net_is_ready() && sys_net_init(0)) sys_net_initialize_stack();
        if (sys_net_is_ready()) sys_net_configure_address();
    } else if (network_action == 2) sys_net_shutdown();
    else if (sys_net_is_ready()) sys_net_configure_address();
    MUTEX_GUARD(kernel_mutex);
    uint8_t leased;
    sys_net_get_configuration(0, 0, &leased);
    load_manual();
    busy = 0;
    request_redraw();
}
static int hit(int x, int y, int bx, int by, int w) {
    return x >= bx && x < bx + w && y >= by && y < by + 28;
}
static void mouse(Window* win, int32_t x, int32_t y, uint8_t buttons) {
    (void)win;
    if (!(buttons & 1)) return;
    for (int i = 0; i < 3; i++) if (hit(x, y, 16 + i * 152, 36, 140)) tab = i;
    if (tab == 0) {
        if (hit(x,y,20,188,40)) mode = (mode + 4) % 5;
        if (hit(x,y,304,188,40)) mode = (mode + 1) % 5;
        if (hit(x,y,356,188,104)) video_request_resolution(widths[mode], heights[mode]);
    } else if (tab == 1 && !busy) {
        for (int i = 0; i < 3; i++) if (hit(x,y,158,144 + i * 36,302)) {
            field = i; replace_field = 1;
        }
        if (hit(x,y,20,310,200)) apply_manual();
        int action = hit(x,y,20,276,140) ? 1 : hit(x,y,172,276,140) ? 2 : hit(x,y,324,276,136) ? 3 : 0;
        if (action) {
            network_action = action; busy = 1;
            if (task_create(network_worker, 0, "settings-net") < 0) {
                busy = 0; set_message("Unable to start network task.");
            }
        }
    } else if (tab == 2) {
        uint32_t volume = audio_get_volume(), count = audio_device_count();
        if (hit(x,y,20,152,220) && count) {
            set_message(audio_device_select((audio_device_selected() + 1) % count) < 0 ?
                "Could not select output device." : "Output device changed.");
        }
        if (hit(x,y,20,244,64)) audio_set_volume(volume > 10 ? volume - 10 : 0);
        if (hit(x,y,96,244,64)) audio_set_volume(volume < 90 ? volume + 10 : 100);
        if (hit(x,y,172,244,140)) {
            if (volume) { saved_volume = volume; audio_set_volume(0); }
            else audio_set_volume(saved_volume);
        }
    }
    request_redraw();
}
void settings_open(int initial_tab) {
    tab = initial_tab >= 0 && initial_tab < 3 ? initial_tab : 0;
    if (!window.title) {
        window = (Window){40, 50, 480, 390, "Settings", 1, 1, draw, key, mouse};
    }
    for (int i = 0; i < 5; i++) if (widths[i] == (uint32_t)SCREEN_WIDTH && heights[i] == (uint32_t)SCREEN_HEIGHT) mode = i;
    if (!busy) load_manual();
    if (wm_register_window(&window) < 0) return;
    wm_bring_to_front(&window); request_redraw();
}
void settings_poll(void) {
    if (message_expires && scheduler_clock() >= message_expires) {
        message = "Nothing to report.";
        message_expires = 0;
        if (window.visible) request_redraw();
    }
    static int last_status;
    int status = video_resolution_status();
    if (status != last_status) { last_status = status; if (window.visible) request_redraw(); }
}

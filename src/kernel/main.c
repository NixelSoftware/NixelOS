#include "types.h"
#include "cursor.h"
#include "font.h"
#include "keyboard.h"
#include "window.h"
#include "../system/graphics/wm/desktop.h"
#include "drivers/display.h"
#include "drivers/serial.h"
#include "drivers/floppy.h"
#include "drivers/power.h"
#include "drivers/ps2.h"
#include "idt.h"
#include "fs.h" 
#include "os_api.h"
#include "task.h"
#include "kernel_net.h"
#include "drivers/net.h"
#include "net/arp.h"
#include "net/icmp.h"
#include "net/ipv4.h"
#include "net/dhcp.h"
#include "net/udp.h"
#include "dialog.h"
#include "startscreen.h"
#include "screenshot.h"
#include "../system/shell/commands.h"
#include "../system/init/nixinit.h"
#include "../system/graphics/wm/wm.h"
#include "../apps/notepad/notepad.h"
#include "../apps/viewer/viewer.h"
#include "../system/graphics/terminal/terminal.h"
#include "../system/services/input/input.h"
#include "../system/loader/app_loader.h"
#include "../system/graphics/session.h"

#define MOUSE_CURSOR_W 12
#define MOUSE_CURSOR_H 19


static void term_print(const char* text);
static void api_put_pixel(int32_t x, int32_t y, uint32_t color);
static void api_move_cursor(int32_t x, int32_t y, uint8_t fast_update);
static void api_render_screen(void);
static uint8_t api_take_redraw_request(void);
static void api_capture_screenshot(void);
static void render_screen(void);
static void api_shell_set_output(shell_output_fn output);
static int api_shell_open_viewer(const char* filename);
static int api_launch_app(const char* name, const char* arguments);
static uint32_t api_get_ticks(void);
static void update_cursor_fast(int32_t new_x, int32_t new_y);
static void cursor_save(int32_t x, int32_t y);
extern void irq0_timer_handler(void);

volatile uint32_t frame_timer = 0;
static int32_t mouse_x = 400;
static int32_t mouse_y = 300;
static int32_t old_mouse_x = 400;
static int32_t old_mouse_y = 300;
static uint32_t cursor_saved_bg[MOUSE_CURSOR_W * MOUSE_CURSOR_H];
static uint8_t cursor_bg_valid = 0;

static uint8_t needs_redraw = 1;


static shell_output_fn shell_output;


static void draw_heartbeat_indicator(void) {
    static const uint32_t colors[4] = {
        0x0000FF00,
        0x00FF0000,
        0x000000FF,
        0x00FFFF00
    };

    uint32_t color = colors[(frame_timer >> 2) & 3];
    display_fill_rect(0, 0, 16, 16, color);
    display_blit_rect(0, 0, 16, 16);
}



static void pit_init(uint32_t frequency_hz) {
    uint32_t divisor = 1193182 / frequency_hz;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    display_fill_rect(x, y, w, h, color);
}

void draw_button(int32_t x, int32_t y, int32_t w, int32_t h, const char* label, uint8_t pressed) {
    fill_rect(x, y, w, h, COLOR_WIN_BG);

    uint32_t top_left = pressed ? COLOR_DARK_GRAY : COLOR_WHITE;
    uint32_t bottom_right = pressed ? COLOR_WHITE : COLOR_DARK_GRAY;

    fill_rect(x, y, w, 1, top_left);
    fill_rect(x, y, 1, h, top_left);
    fill_rect(x, y + h - 1, w, 1, bottom_right);
    fill_rect(x + w - 1, y, 1, h, bottom_right);

    if (label) {
        int offset = pressed ? 1 : 0;
        draw_string(x + 6 + offset, y + (h / 2) - 4 + offset, label, COLOR_BLACK);
    }
}

static os_api_t kernel_api = {
    .graphics = {
        .fill_rect = fill_rect,
        .put_pixel = api_put_pixel,
        .draw_string = draw_string,
        .draw_button = draw_button,
        .render_screen = api_render_screen,
        .take_redraw_request = api_take_redraw_request,
        .capture_screenshot = api_capture_screenshot,
        .blit_rect = display_blit_rect,
        .blit_all = display_blit,
        .png_decode_and_resize = png_decode_and_resize,
        .png_render = png_render
    },
    .input = {
        .ps2_status = ps2_controller_status,
        .ps2_read_data = ps2_controller_read_data,
        .ps2_initialize_mouse = ps2_mouse_initialize,
        .poll_input = input_poll_event,
        .translate_scancode = kbd_scancode_to_ascii,
        .move_cursor = api_move_cursor
    },
    .fs = {
        .list = fs_list,
        .read = fs_read_file,
        .write = fs_write_file,
        .delete_file = fs_delete_file
    },
    .shell = {
        .term_print = term_print,
        .command_args = shell_command_args,
        .shell_set_output = api_shell_set_output,
        .shell_open_viewer = api_shell_open_viewer,
        .launch_app = api_launch_app
    },
    .network = {
        .net_poll = sys_net_poll,
        .get_ticks = api_get_ticks,
        .icmp_request_echo = icmp_request_echo,
        .icmp_set_echo_reply_handler = icmp_set_echo_reply_handler,
        .net_is_ready = sys_net_is_ready,
        .net_get_diagnostics = sys_net_get_diagnostics,
        .get_mac_address = sys_net_get_mac_address,
        .get_configuration = sys_net_get_configuration,
        .ipv4_send = ipv4_send,
        .ipv4_set_handler = ipv4_set_protocol_handler,
        .ipv4_get_local_ip = ipv4_get_local_ip,
        .dns_resolve = dns_resolve
    },
    .windows = {
        .register_window = wm_register_window,
        .unregister_window = wm_unregister_window,
        .show_info_dialog = show_info_dialog
    },
    .devices = {
        .floppy_init = floppy_init,
        .floppy_read = floppy_read_sectors,
        .floppy_write = floppy_write_sectors
    },
    .system = {
        .shutdown = power_shutdown,
        .reboot = power_reboot
    }
};

static void api_put_pixel(int32_t x, int32_t y, uint32_t color) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    uint32_t* buffer = display_get_backbuffer();
    buffer[y * SCREEN_WIDTH + x] = color;
}

static void api_move_cursor(int32_t x, int32_t y, uint8_t fast_update) {
    mouse_x = x;
    mouse_y = y;
    if (fast_update) update_cursor_fast(x, y);
}

static void api_render_screen(void) {
    render_screen();
    needs_redraw = 0;
}

static uint8_t api_take_redraw_request(void) {
    uint8_t pending = needs_redraw;
    needs_redraw = 0;
    return pending;
}

static void api_capture_screenshot(void) {
    debug_print("[KBD] Triggering Screenshot Capture via Function Key!\n");
    screenshot_capture();
}

static void api_shell_set_output(shell_output_fn output) {
    shell_output = output;
}

static int api_shell_open_viewer(const char* filename) {
    if (!viewer_load_bmp(filename)) return 0;
    viewer_window.visible = 1;
    wm_bring_to_front(&viewer_window);
    needs_redraw = 1;
    return 1;
}

static int api_launch_app(const char* name, const char* arguments) {
    int result = app_loader_launch(&kernel_api, name, arguments);
    if (result == 0) needs_redraw = 1;
    return result;
}

static void term_print(const char* text) {
    if (shell_output) shell_output(text);
}

static uint32_t api_get_ticks(void) {
    return frame_timer;
}

static void cursor_save(int32_t x, int32_t y) {
    uint32_t* buf = display_get_backbuffer();
    for (int r = 0; r < MOUSE_CURSOR_H; r++) {
        int py = y + r;
        if (py < 0 || py >= SCREEN_HEIGHT) continue;
        for (int c = 0; c < MOUSE_CURSOR_W; c++) {
            int px = x + c;
            if (px < 0 || px >= SCREEN_WIDTH) continue;
            cursor_saved_bg[r * MOUSE_CURSOR_W + c] = buf[py * SCREEN_WIDTH + px];
        }
    }
    cursor_bg_valid = 1;
}

static void cursor_restore(int32_t x, int32_t y) {
    if (!cursor_bg_valid) return;
    uint32_t* buf = display_get_backbuffer();
    for (int r = 0; r < MOUSE_CURSOR_H; r++) {
        int py = y + r;
        if (py < 0 || py >= SCREEN_HEIGHT) continue;
        for (int c = 0; c < MOUSE_CURSOR_W; c++) {
            int px = x + c;
            if (px < 0 || px >= SCREEN_WIDTH) continue;
            buf[py * SCREEN_WIDTH + px] = cursor_saved_bg[r * MOUSE_CURSOR_W + c];
        }
    }
    display_blit_rect(x, y, MOUSE_CURSOR_W, MOUSE_CURSOR_H);
}

static void update_cursor_fast(int32_t new_x, int32_t new_y) {
    if (new_x == old_mouse_x && new_y == old_mouse_y) return;

    cursor_restore(old_mouse_x, old_mouse_y);
    cursor_save(new_x, new_y);
    draw_cursor(new_x, new_y);
    display_blit_rect(new_x, new_y, MOUSE_CURSOR_W, MOUSE_CURSOR_H);

    old_mouse_x = new_x;
    old_mouse_y = new_y;
}

static void render_screen(void) {
    display_clear(COLOR_DESKTOP);
    desktop_draw();

    wm_draw_windows();
    wm_draw_taskbar(wm_start_menu_is_open());

    cursor_save(mouse_x, mouse_y);
    draw_cursor(mouse_x, mouse_y);
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;

    display_blit();
}

static void nixinit_show_status(const char* text, int percent) {
    startscreen_show(text, percent);
}

static void kernel_init_interrupts(void) {
    debug_print("[INIT] IDT init...\n");
    idt_init();
}

static void kernel_init_timer(void) {
    debug_print("[INIT] PIT init...\n");
    pit_init(100);
}

static void kernel_init_network_device(void) {
    debug_print("[INIT] Net init...\n");
    sys_net_init(0);
}

static void kernel_init_network_stack(void) {
    sys_net_initialize_stack();
}

static void kernel_configure_network(void) {
    sys_net_configure_address();
}

static void kernel_mount_filesystem(void) {
    debug_print("[INIT] FS init...\n");
    fs_init();
}

static void kernel_initialize_tasking(void) {
    debug_print("[INIT] Tasking init...\n");
    tasking_init();
}

static void kernel_mark_redraw(void) {
    needs_redraw = 1;
}

static void kernel_initialize_system_apps(void) {
    debug_print("[INIT] Windows+Apps init...\n");
    system_graphics_initialize(&kernel_api, kernel_mark_redraw);
}

static void kernel_initialize_input(void) {
    debug_print("[INIT] PS/2 Mouse init...\n");
    input_initialize(&kernel_api);
}

static void kernel_finish_boot(void) {
    startscreen_stop();
    debug_print("[INIT] Drawing to display...\n");
    render_screen();
    needs_redraw = 0;
    debug_print("[INIT] Enable interrupt...\n");
    __asm__ volatile ("sti");
}

void kernel_main(uint32_t lfb_address, uint32_t bpp, uint32_t pitch) {
    __asm__ volatile ("cli");
    
    serial_init();
    debug_print("\n=== SYSTEM BOOTING ===\n");
    debug_print("[VBE] LFB Address: "); debug_hex(lfb_address); debug_print("\n");
    debug_print("[VBE] BPP: ");         debug_hex(bpp);         debug_print("\n");
    debug_print("[VBE] Pitch: ");       debug_hex(pitch);       debug_print("\n");

    debug_print("[INIT] Display init...\n");

    display_init(lfb_address, bpp, pitch);

    nixinit_services_t init_services = {
        .show_status = nixinit_show_status,
        .initialize_interrupts = kernel_init_interrupts,
        .initialize_timer = kernel_init_timer,
        .initialize_network_device = kernel_init_network_device,
        .initialize_network_stack = kernel_init_network_stack,
        .configure_network = kernel_configure_network,
        .mount_filesystem = kernel_mount_filesystem,
        .initialize_tasking = kernel_initialize_tasking,
        .initialize_system_apps = kernel_initialize_system_apps,
        .initialize_input = kernel_initialize_input,
        .finish_boot = kernel_finish_boot
    };
    nixinit_start(&init_services);

    debug_print("[INIT] Entering system event loop.\n");
    for (;;) {
        sys_net_poll();
        wm_poll();
    }
}

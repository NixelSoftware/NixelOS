#include "../system/services/audio/system_sound.h"
#include "types.h"
#include "../system/graphics/wm/cursor.h"
#include "drivers/display/font.h"
#include "keyboard.h"
#include "window.h"
#include "../system/graphics/wm/desktop.h"
#include "drivers/display.h"
#include "drivers/serial.h"
#include "drivers/floppy.h"
#include "drivers/power.h"
#include "drivers/acpi.h"
#include "drivers/ps2.h"
#include "drivers/sb16_driver.h"
#include "idt.h"
#include "fs.h" 
#include "os_api.h"
#include "task.h"
#include "scheduler.h"
#include "mutex.h"
#include "paging.h"
#include "arch_i386.h"
#include "syscall.h"
#include "kernel_net.h"
#include "drivers/net.h"
#include "net/arp.h"
#include "net/icmp.h"
#include "net/ipv4.h"
#include "net/dhcp.h"
#include "net/udp.h"
#include "../system/graphics/dialog/dialog.h"
#include "../system/init/splash/startscreen.h"
#include "../system/graphics/screenshot/screenshot.h"
#include "../system/shell/commands.h"
#include "../system/init/nixinit.h"
#include "../system/graphics/wm/wm.h"
#include "../apps/notepad/notepad.h"
#include "../apps/viewer/viewer.h"
#include "../system/graphics/terminal/terminal.h"
#include "../system/services/input/input.h"
#include "../system/loader/app_loader.h"
#include "../system/graphics/session.h"
#include "../system/graphics/widgets.h"

static void term_print(const char* text);
void kernel_user_write(const char* text, uint32_t length);
static void api_put_pixel(int32_t x, int32_t y, uint32_t color);
static void api_move_cursor(int32_t x, int32_t y, uint8_t fast_update);
static void api_render_screen(void);
static uint8_t api_take_redraw_request(void);
static void api_capture_screenshot(void);
static void api_shell_set_output(shell_output_fn output);
static int api_shell_open_viewer(const char* filename);
static int api_launch_app(const char* name, const char* arguments);
static uint32_t api_get_ticks(void);
static uint8_t api_ipv4_send(uint32_t destination_ip, uint8_t protocol,
                             const uint8_t* payload, uint32_t length);
static uint8_t api_dns_resolve(const char* hostname, uint32_t* address);
extern void irq0_timer_handler(void);

volatile uint32_t frame_timer = 0;
static uint8_t needs_redraw = 1;


static shell_output_fn shell_output;


static void pit_init(uint32_t frequency_hz) {
    uint32_t divisor = 1193182 / frequency_hz;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

static os_api_t kernel_api = {
    .graphics = {
        .fill_rect = display_fill_rect,
        .put_pixel = api_put_pixel,
        .draw_string = draw_string,
        .draw_button = graphics_draw_button,
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
        .get_device_info = sys_net_get_device_info,
        .net_poll = sys_net_poll,
        .get_ticks = api_get_ticks,
        .icmp_request_echo = icmp_request_echo,
        .icmp_set_echo_reply_handler = icmp_set_echo_reply_handler,
        .net_is_ready = sys_net_is_ready,
        .net_get_diagnostics = sys_net_get_diagnostics,
        .get_mac_address = sys_net_get_mac_address,
        .get_configuration = sys_net_get_configuration,
        .ipv4_send = api_ipv4_send,
        .ipv4_set_handler = ipv4_set_protocol_handler,
        .ipv4_get_local_ip = ipv4_get_local_ip,
        .dns_resolve = api_dns_resolve
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
    MUTEX_GUARD(kernel_mutex);
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    uint32_t* buffer = display_get_backbuffer();
    buffer[y * SCREEN_WIDTH + x] = color;
}

static void api_move_cursor(int32_t x, int32_t y, uint8_t fast_update) {
    wm_cursor_move(x, y, fast_update);
}

static void api_render_screen(void) {
    MUTEX_GUARD(kernel_mutex);
    wm_render_screen();
    needs_redraw = 0;
}

static uint8_t api_take_redraw_request(void) {
    MUTEX_GUARD(kernel_mutex);
    uint8_t pending = needs_redraw;
    needs_redraw = 0;
    return pending;
}

static void api_capture_screenshot(void) {
    debug_print("[KBD] Triggering Screenshot Capture via Function Key!\n");
    screenshot_capture();
}

static void api_shell_set_output(shell_output_fn output) {
    MUTEX_GUARD(kernel_mutex);
    shell_output = output;
}

static int api_shell_open_viewer(const char* filename) {
    if (!viewer_load_bmp(filename)) return 0;
    MUTEX_GUARD(kernel_mutex);
    viewer_window.visible = 1;
    wm_bring_to_front(&viewer_window);
    needs_redraw = 1;
    return 1;
}

static int api_launch_app(const char* name, const char* arguments) {
    MUTEX_GUARD(kernel_mutex);
    int result = app_loader_launch(&kernel_api, name, arguments);
    if (result == 0) needs_redraw = 1;
    return result;
}

static void term_print(const char* text) {
    uint32_t length = 0;
    while (text[length]) length++;
    kernel_user_write(text, length);
}

void kernel_user_write(const char* text, uint32_t length) {
    MUTEX_GUARD(kernel_mutex);
    terminal_write(text, length);
    needs_redraw = 1;
}

static uint32_t api_get_ticks(void) {
    return frame_timer;
}

static uint8_t api_ipv4_send(uint32_t destination_ip, uint8_t protocol,
                             const uint8_t* payload, uint32_t length) {
    uint8_t sent = ipv4_send(destination_ip, protocol, payload, length);
    if (paging_current_directory() != paging_kernel_directory()) {
        debug_print("[APP NET] IPv4 send result: ");
        debug_hex(sent);
        debug_print(" destination: ");
        debug_hex(destination_ip);
        debug_print(" local: ");
        debug_hex(ipv4_get_local_ip());
        debug_print(" protocol: ");
        debug_hex(protocol);
        debug_print("\n");
    }
    return sent;
}

static uint8_t api_dns_resolve(const char* hostname, uint32_t* address) {
    uint8_t resolved = dns_resolve(hostname, address);
    if (paging_current_directory() != paging_kernel_directory()) {
        debug_print("[APP NET] DNS result for ");
        debug_print(hostname);
        debug_print(": ");
        debug_hex(resolved);
        if (resolved && address) {
            debug_print(" address: ");
            debug_hex(*address);
        }
        debug_print("\n");
    }
    return resolved;
}

static void nixinit_show_status(const char* text, int percent) {
    startscreen_show(text, percent);
}

static void kernel_init_interrupts(void) {
    debug_print("[INIT] GDT/TSS init...\n");
    arch_i386_init();
    debug_print("[INIT] IDT init...\n");
    idt_init();
}

static void kernel_init_timer(void) {
    debug_print("[INIT] PIT init...\n");
    pit_init(100);
    /* The bootloader masks every PIC line; PIT programming alone cannot
     * deliver ticks for scheduling, sleeps, and network/audio deadlines. */
    outb(0x21, inb(0x21) & ~1u);
}

static void kernel_init_acpi(void) {
    debug_print("[INIT] ACPI init...\n");
    acpi_init();
}

static void kernel_init_network_device(void) {
    debug_print("[INIT] Net init...\n");
    uint8_t initialized = sys_net_init(0);
    debug_print("[INIT] Net init returned: ");
    debug_hex(initialized);
    debug_print("\n");
}

static void kernel_init_network_stack(void) {
    debug_print("[INIT] Net stack init entered.\n");
    sys_net_initialize_stack();
    debug_print("[INIT] Net stack init returned.\n");
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
    MUTEX_GUARD(kernel_mutex);
    needs_redraw = 1;
}

static void kernel_initialize_system_apps(void) {
    debug_print("[INIT] Windows+Apps init...\n");
    debug_subsystem_print("[INIT] api_put_pixel code address: ");
    debug_subsystem_hex((uint32_t)api_put_pixel);
    debug_subsystem_print("\n[INIT] kernel API put_pixel field: ");
    debug_subsystem_hex((uint32_t)kernel_api.graphics.put_pixel);
    debug_subsystem_print("\n");
    system_graphics_initialize(&kernel_api, kernel_mark_redraw);
}

static void kernel_initialize_input(void) {
    debug_print("[INIT] PS/2 Mouse init...\n");
    desktop_trace_api("before input init");
    debug_subsystem_print("[INIT] put_pixel before input init: ");
    debug_subsystem_hex((uint32_t)kernel_api.graphics.put_pixel);
    debug_subsystem_print("\n");
    input_initialize(&kernel_api);
    desktop_trace_api("after input init");
    debug_subsystem_print("[INIT] put_pixel after input init: ");
    debug_subsystem_hex((uint32_t)kernel_api.graphics.put_pixel);
    debug_subsystem_print("\n");
}

static void kernel_finish_boot(void) {
    desktop_trace_api("before startscreen_stop");
    startscreen_stop();
    desktop_trace_api("after startscreen_stop");
    debug_subsystem_print("[INIT] Display backbuffer address: ");
    debug_subsystem_hex((uint32_t)display_get_backbuffer());
    debug_subsystem_print("\n");
    debug_print("[INIT] Drawing to display...\n");
    wm_render_screen();
    debug_subsystem_print("[INIT] Display drawing returned.\n");
    needs_redraw = 0;
    debug_print("[INIT] Enable interrupt...\n");
    __asm__ volatile ("sti");


}

static void network_service(void* arg) {
    (void)arg;
    for (;;) { sys_net_poll(); task_sleep_ms(2); }
}
static void audio_service(void* arg) {
    (void)arg;
    system_sound_start();
    for (;;) { system_sound_poll(); sb16_service(); task_sleep_ms(2); }
}
static void cleanup_service(void* arg) {
    (void)arg;
    for (;;) { app_loader_collect(); task_sleep_ms(20); }
}

void kernel_main(uint32_t lfb_address, uint32_t bpp, uint32_t pitch) {
    __asm__ volatile ("cli");

    paging_init();
    
    startup_trace_init();
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
        .initialize_acpi = kernel_init_acpi,
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

    #if defined(ENABLE_OOBE) && ENABLE_OOBE == 1
    char oobe_check[1];
    if (fs_read_file("OOBE.DON", (uint8_t*)oobe_check, 1) < 0) {
        debug_print("[OOBE] First boot detected. Launching setup wizard...\n");
        api_launch_app("oobe.bin", "");
    }
    #endif
    
    /* Every service has its own schedulable context. Only the idle task halts;
     * ready applications never wait behind an unconditional HLT in the WM. */
#if STARTUP_PROFILE == 0 || STARTUP_PROFILE == 2
    if (task_create_nice(network_service, 0, "network", -2) < 0 ||
#if STARTUP_PROFILE == 0
        task_create_nice(audio_service, 0, "audio", -4) < 0 ||
#endif
        task_create_nice(cleanup_service, 0, "reaper", 5) < 0 ||
        task_create_nice(shell_service, 0, "shell", 0) < 0 ||
        display_start_async() < 0 || serial_start_async() < 0) {
#else
    if (serial_start_async() < 0) {
#endif
        debug_print("[INIT] Cannot allocate service tasks\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    scheduler_start();
    debug_print("[INIT] CFS scheduler started: 1000 Hz, independent services\n");
    for (;;) {
#if STARTUP_PROFILE == 1
        startup_trace_poll(1);
        os_input_event_t event;
        for (unsigned budget=16; budget && input_poll_event(&event); budget--) { }
        startup_trace_poll(0);
#elif STARTUP_PROFILE == 3
        /* Idle heartbeat has the same sleep/wake cadence as the input loop. */
        startup_trace_poll(1);
        startup_trace_poll(0);
#else
        wm_poll();
#endif
        task_sleep_ms(2);
    }
}

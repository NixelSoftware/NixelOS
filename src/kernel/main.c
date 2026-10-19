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

#define TASKBAR_Y       568
#define TASKBAR_HEIGHT  32

#define MOUSE_CURSOR_W 12
#define MOUSE_CURSOR_H 19


static void term_print(const char* text);
static void api_put_pixel(int32_t x, int32_t y, uint32_t color);
static void api_shell_set_output(shell_output_fn output);
static int api_shell_open_viewer(const char* filename);
static int api_shell_launch_app(const char* name, const char* arguments);
static uint32_t api_get_ticks(void);
static int str_prefix(const char* str, const char* prefix);
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

static Window* dragging_window = 0;
static uint8_t prev_left_click = 0;
static uint8_t start_menu_open = 0;
static uint8_t needs_redraw = 1;


static volatile uint8_t ping_reply_received;
static volatile uint32_t ping_reply_time;
static uint32_t ping_target_ip;
static uint32_t ping_start_time;
static uint16_t ping_sequence;
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



static void print_u32(uint32_t value) {
    char digits[11];
    int count = 0;
    if (value == 0) {
        term_print("0");
        return;
    }

    while (value != 0) {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (count > 0) {
        char digit[2] = { digits[--count], '\0' };
        term_print(digit);
    }
}

static void print_ip(uint32_t address) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        print_u32((address >> shift) & 0xFF);
        if (shift != 0) term_print(".");
    }
}

static void print_hex_byte(uint8_t value) {
    const char* digits = "0123456789ABCDEF";
    char text[3] = { digits[value >> 4], digits[value & 0x0F], '\0' };
    term_print(text);
}

static void terminal_ifconfig(void) {
    uint64_t mac = sys_net_get_mac_address();
    const dhcp_lease_t* lease = dhcp_get_lease();
    uint32_t mask = lease->leased ? lease->subnet_mask : 0xFFFFFF00;
    uint32_t gateway = lease->leased ? lease->gateway : 0x0a000002;
    term_print("eth0\n  Link: ");
    term_print(sys_net_is_ready() ? "up\n" : "down\n");
    term_print("  MAC: ");
    for (int i = 5; i >= 0; i--) {
        print_hex_byte((uint8_t)(mac >> (i * 8)));
        if (i != 0) term_print(":");
    }
    term_print("\n  IP: ");
    print_ip(sys_net_get_ip_address());
    term_print("\n  Netmask: ");
    print_ip(mask);
    term_print("\n  Gateway: ");
    print_ip(gateway);
    term_print("\n  DHCP: ");
    term_print(lease->leased ? "leased\n" : "not available\n");
}

static void terminal_netdiag(void) {
    net_diagnostics_t diagnostics;
    //net_poll();
    sys_net_poll();
    net_get_diagnostics(&diagnostics);

    term_print("Network driver diagnostics\n");
    term_print("  IO base: 0x");
    print_hex_byte((uint8_t)(diagnostics.io_base >> 8));
    print_hex_byte((uint8_t)diagnostics.io_base);
    term_print("\n  State: ");
    term_print(net_is_ready() ? "ready\n" : "not ready\n");
    term_print("  Init failure: ");
    print_u32(diagnostics.init_failure);
    term_print("\n");
    term_print("  Probe SW/DW CSR0: 0x");
    print_hex_byte((uint8_t)(diagnostics.sw_csr0 >> 8));
    print_hex_byte((uint8_t)diagnostics.sw_csr0);
    term_print("/0x");
    print_hex_byte((uint8_t)(diagnostics.dw_csr0 >> 24));
    print_hex_byte((uint8_t)(diagnostics.dw_csr0 >> 16));
    print_hex_byte((uint8_t)(diagnostics.dw_csr0 >> 8));
    print_hex_byte((uint8_t)diagnostics.dw_csr0);
    term_print("  CSR0: 0x");
    print_hex_byte((uint8_t)(diagnostics.csr0 >> 8));
    print_hex_byte((uint8_t)diagnostics.csr0);
    term_print("\n  TX attempts/submitted/busy: ");
    print_u32(diagnostics.tx_attempts);
    term_print("/");
    print_u32(diagnostics.tx_submitted);
    term_print("/");
    print_u32(diagnostics.tx_busy);
    term_print("\n  TX completed/errors: ");
    print_u32(diagnostics.tx_completed);
    term_print("/");
    print_u32(diagnostics.tx_errors);
    term_print("\n  Last TX status/misc: 0x");
    print_hex_byte((uint8_t)(diagnostics.last_tx_status >> 8));
    print_hex_byte((uint8_t)diagnostics.last_tx_status);
    term_print("/0x");
    print_hex_byte((uint8_t)(diagnostics.last_tx_misc >> 24));
    print_hex_byte((uint8_t)(diagnostics.last_tx_misc >> 16));
    print_hex_byte((uint8_t)(diagnostics.last_tx_misc >> 8));
    print_hex_byte((uint8_t)diagnostics.last_tx_misc);
    term_print("\n  TX descriptor address/length/status: 0x");
    print_hex_byte((uint8_t)(diagnostics.tx_descriptor_address >> 24));
    print_hex_byte((uint8_t)(diagnostics.tx_descriptor_address >> 16));
    print_hex_byte((uint8_t)(diagnostics.tx_descriptor_address >> 8));
    print_hex_byte((uint8_t)diagnostics.tx_descriptor_address);
    term_print("/");
    print_u32((uint32_t)(uint16_t)diagnostics.tx_descriptor_length);
    term_print("/0x");
    print_hex_byte((uint8_t)(diagnostics.tx_descriptor_status >> 8));
    print_hex_byte((uint8_t)diagnostics.tx_descriptor_status);
    term_print("\n  RX frames/errors/invalid: ");
    print_u32(diagnostics.rx_frames);
    term_print("/");
    print_u32(diagnostics.rx_errors);
    term_print("/");
    print_u32(diagnostics.rx_invalid);
    term_print("\n  RX descriptor status/length: 0x");
    print_hex_byte((uint8_t)(diagnostics.rx_descriptor_status >> 8));
    print_hex_byte((uint8_t)diagnostics.rx_descriptor_status);
    term_print("/");
    print_u32(diagnostics.rx_message_length);
    term_print("\n  Last RX length: ");
    print_u32(diagnostics.last_rx_length);
    term_print("\n  Last error CSR0: 0x");
    print_hex_byte((uint8_t)(diagnostics.last_error_status >> 8));
    print_hex_byte((uint8_t)diagnostics.last_error_status);
    term_print("\n  Ring indexes TX/RX: ");
    print_u32(diagnostics.tx_index);
    term_print("/");
    print_u32(diagnostics.rx_index);
    term_print("\n");
}

static uint8_t tick_before(uint32_t value, uint32_t deadline) {
    return (int32_t)(value - deadline) < 0;
}

static int parse_ipv4(const char* text, uint32_t* address) {
    uint32_t result = 0;
    int parts = 0;

    while (*text != '\0' && parts < 4) {
        uint32_t value = 0;
        int digits = 0;
        while (*text >= '0' && *text <= '9') {
            value = value * 10 + (uint32_t)(*text - '0');
            if (value > 255) return 0;
            text++;
            digits++;
        }
        if (digits == 0) return 0;
        result = (result << 8) | value;
        parts++;
        if (parts < 4) {
            if (*text != '.') return 0;
            text++;
        }
    }
    if (parts != 4 || *text != '\0') return 0;
    *address = result;
    return 1;
}

static void ping_reply(uint32_t source_ip, uint16_t identifier,
                       uint16_t sequence, const uint8_t* payload,
                       uint16_t length) {
    (void)identifier;
    (void)payload;
    if (source_ip == ping_target_ip && sequence == ping_sequence) {
        ping_reply_time = frame_timer - ping_start_time;
        ping_reply_received = 1;
        (void)length;
    }
}

static void terminal_ping(const char* argument) {
    uint32_t address;
    uint32_t received = 0;
    uint32_t total = 0;
    uint32_t minimum = 0xFFFFFFFF;
    uint32_t maximum = 0;
    char host[32];
    int host_length = 0;
    const uint8_t payload[] = "operating-system";

    if (str_prefix(argument, "http://")) argument += 7;
    else if (str_prefix(argument, "https://")) argument += 8;
    while (argument[host_length] != '\0'
           && argument[host_length] != '/'
           && argument[host_length] != ' '
           && host_length < (int)sizeof(host) - 1) {
        host[host_length] = argument[host_length];
        host_length++;
    }
    host[host_length] = '\0';

    if (!parse_ipv4(host, &address)) {
        term_print("Usage: ping <IPv4 address>\n");
        term_print("URL paths are accepted for IPv4 URLs; DNS is not supported.\n");
        return;
    }

    term_print("PING ");
    term_print(host);
    term_print(" (4 packets)\n");

    ping_target_ip = address;
    for (ping_sequence = 1; ping_sequence <= 4; ping_sequence++) {
        uint32_t start = frame_timer;
        uint32_t deadline = start + 200;
        uint32_t next_attempt = start;
        uint8_t sent = 0;
        ping_reply_received = 0;
        ping_start_time = start;

        while (!sent && tick_before(frame_timer, deadline)) {
            //net_poll();
            sys_net_poll();
            if (!tick_before(frame_timer, next_attempt)) {
                sent = icmp_request_echo(address, 0x4F53, ping_sequence,
                                         payload, sizeof(payload) - 1);
                next_attempt = frame_timer + 10;
            }
            if (!sent) {
                for (volatile uint32_t delay = 0; delay < 2000; delay++)
                    __asm__ volatile ("pause");
            }
        }

        while (sent && !ping_reply_received
               && tick_before(frame_timer, deadline)) {
            //net_poll();
            sys_net_poll();
            for (volatile uint32_t delay = 0; delay < 2000; delay++)
                __asm__ volatile ("pause");
        }

        if (ping_reply_received) {
            uint32_t milliseconds = ping_reply_time * 10;
            term_print("reply ");
            print_u32(ping_sequence);
            term_print(": ");
            print_u32(milliseconds);
            term_print(" ms\n");
            received++;
            total += milliseconds;
            if (milliseconds < minimum) minimum = milliseconds;
            if (milliseconds > maximum) maximum = milliseconds;
        } else {
            term_print("timeout ");
            print_u32(ping_sequence);
            term_print("\n");
        }
    }

    term_print("--- ping statistics ---\n");
    print_u32(4 - received);
    term_print(" packets lost (");
    print_u32((4 - received) * 25);
    term_print("%)\n");
    if (received != 0) {
        term_print("min/avg/max = ");
        print_u32(minimum);
        term_print("/");
        print_u32(total / received);
        term_print("/");
        print_u32(maximum);
        term_print(" ms\n");
    }
}


static uint8_t mouse_cycle = 0;
static uint8_t mouse_packet[3];

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

static uint8_t child_poll_events(int32_t* out_mx, int32_t* out_my, uint8_t* out_btn) {
    uint8_t scancode_out = 0;

    uint8_t status = inb(0x64);
    if (status & 0x01) {
        if (status & 0x20) {
            uint8_t b = inb(0x60);
            if (mouse_cycle == 0) {
                if (b & 0x08) {
                    mouse_packet[0] = b;
                    mouse_cycle = 1;
                }
            } else if (mouse_cycle == 1) {
                mouse_packet[1] = b;
                mouse_cycle = 2;
            } else if (mouse_cycle == 2) {
                mouse_packet[2] = b;
                mouse_cycle = 0;

                uint8_t flags = mouse_packet[0];
                int8_t dx = (int8_t)mouse_packet[1];
                int8_t dy = (int8_t)mouse_packet[2];

                if (!(flags & 0xC0)) {
                    if (dx != 0 || dy != 0) {
                        mouse_x += dx;
                        mouse_y -= dy;
                        if (mouse_x < 0) mouse_x = 0;
                        if (mouse_x > SCREEN_WIDTH - 12) mouse_x = SCREEN_WIDTH - 12;
                        if (mouse_y < 0) mouse_y = 0;
                        if (mouse_y > SCREEN_HEIGHT - 19) mouse_y = SCREEN_HEIGHT - 19;

                        update_cursor_fast(mouse_x, mouse_y);
                    }
                    prev_left_click = flags & 0x01;
                }
            }
        } else {
            uint8_t sc = inb(0x60);
            if (!(sc & 0x80)) {
                scancode_out = sc;
            }
        }
    }

    if (out_mx)  *out_mx = mouse_x;
    if (out_my)  *out_my = mouse_y;
    if (out_btn) *out_btn = prev_left_click;

    return scancode_out;
}

static void api_sync_cursor(void) {
    cursor_save(mouse_x, mouse_y);
    draw_cursor(mouse_x, mouse_y);
    display_blit_rect(mouse_x, mouse_y, MOUSE_CURSOR_W, MOUSE_CURSOR_H);
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;
}

static os_api_t kernel_api = {
    .fill_rect          = fill_rect,
    .put_pixel          = api_put_pixel,
    .draw_string        = draw_string,
    .draw_button        = draw_button,
    .blit_rect          = display_blit_rect,
    .blit_all           = display_blit,
    .png_decode_and_resize = png_decode_and_resize,
    .png_render         = png_render,
    .fs_list            = fs_list,
    .fs_read            = fs_read_file,
    .fs_write           = fs_write_file,
    .term_print         = term_print,
    .register_window    = wm_register_window,
    .unregister_window  = wm_unregister_window,
    .show_info_dialog = show_info_dialog,
    .fs_delete = fs_delete_file,
    .shell_execute     = shell_execute_command,
    .command_args      = shell_command_args,
    .shell_set_output  = api_shell_set_output,
    .shell_run_ifconfig = terminal_ifconfig,
    .shell_run_netdiag = terminal_netdiag,
    .shell_run_ping    = terminal_ping,
    .shell_open_viewer = api_shell_open_viewer,
    .shell_launch_app  = api_shell_launch_app,
    .net_poll          = sys_net_poll,
    .get_ticks         = api_get_ticks,
    .icmp_request_echo = icmp_request_echo,
    .icmp_set_echo_reply_handler = icmp_set_echo_reply_handler,
    .net_is_ready      = sys_net_is_ready,
    .net_get_diagnostics = sys_net_get_diagnostics,
    .floppy_init  = floppy_init,
    .floppy_read  = floppy_read_sectors,
    .floppy_write = floppy_write_sectors,
    .ipv4_send = ipv4_send,
    .ipv4_set_handler = ipv4_set_protocol_handler,
    .ipv4_get_local_ip = ipv4_get_local_ip,
    .dns_resolve = dns_resolve,
    .shutdown = power_shutdown,
    .reboot   = power_reboot
};

static void api_put_pixel(int32_t x, int32_t y, uint32_t color) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    uint32_t* buffer = display_get_backbuffer();
    buffer[y * SCREEN_WIDTH + x] = color;
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

typedef int (*app_entry_fn)(os_api_t* api);

static int api_shell_launch_app(const char* name, const char* arguments) {
    char filename[FS_MAX_FILENAME];
    int name_length = 0;
    while (name[name_length] && name_length < FS_MAX_FILENAME - 5) {
        filename[name_length] = name[name_length];
        name_length++;
    }
    filename[name_length] = '\0';

    uint8_t* app_memory = (uint8_t*)0x00200000;
    int bytes = fs_read_file(filename, app_memory, 1048576);
    if (bytes <= 0) {
        filename[name_length++] = '.';
        filename[name_length++] = 'b';
        filename[name_length++] = 'i';
        filename[name_length++] = 'n';
        filename[name_length] = '\0';
        bytes = fs_read_file(filename, app_memory, 1048576);
    }
    if (bytes <= (int)sizeof(app_header_t)) return -1;

    app_header_t* header = (app_header_t*)app_memory;
    if (header->magic != APP_HEADER_MAGIC ||
        (header->type != APP_TYPE_GUI && header->type != APP_TYPE_CONSOLE)) return -2;

    uint32_t bss_start = (uint32_t)app_memory + bytes;
    uint32_t bss_end = 0x003F0000;
    if (bss_start < bss_end) {
        uint8_t* bss = (uint8_t*)bss_start;
        for (uint32_t i = 0; i < bss_end - bss_start; i++) bss[i] = 0;
    }

    (void)arguments;
    app_entry_fn entry = (app_entry_fn)(app_memory + sizeof(app_header_t));
    entry(&kernel_api);
    needs_redraw = 1;
    return 0;
}

static void term_print(const char* text) {
    if (shell_output) shell_output(text);
}

static uint32_t api_get_ticks(void) {
    return frame_timer;
}

static int str_prefix(const char* str, const char* prefix) {
    while (*prefix) {
        if (*str != *prefix) return 0;
        str++;
        prefix++;
    }
    return 1;
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
    wm_draw_taskbar(start_menu_open);

    cursor_save(mouse_x, mouse_y);
    draw_cursor(mouse_x, mouse_y);
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;

    display_blit();
}

static int ps2_wait_write(void) {
    for (volatile int i = 0; i < 2000; i++) {
        if (!(inb(0x64) & 0x02)) return 1;
    }
    return 0;
}

static int ps2_wait_read(void) {
    for (volatile int i = 0; i < 2000; i++) {
        if (inb(0x64) & 0x01) return 1;
    }
    return 0;
}

static void ps2_flush_buffer(void) {
    for (volatile int i = 0; i < 100; i++) {
        if (inb(0x64) & 0x01) {
            inb(0x60);
        } else {
            break;
        }
    }
}

static void ps2_send_mouse_cmd(uint8_t cmd) {
    if (!ps2_wait_write()) return;
    outb(0x64, 0xD4);

    if (!ps2_wait_write()) return;
    outb(0x60, cmd);

    if (ps2_wait_read()) {
        inb(0x60);
    }
}

void init_mouse(void) {
    ps2_flush_buffer();

    if (ps2_wait_write()) outb(0x64, 0xA8);
    if (ps2_wait_write()) outb(0x64, 0x20);

    if (ps2_wait_read()) {
        uint8_t config = inb(0x60);
        config |= 0x02;
        config &= ~0x20;

        if (ps2_wait_write()) outb(0x64, 0x60);
        if (ps2_wait_write()) outb(0x60, config);
    }

    ps2_send_mouse_cmd(0xF6);
    ps2_send_mouse_cmd(0xF4);

    ps2_flush_buffer();
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
    uint64_t mac_value = net_get_mac_address();
    uint8_t mac[6];
    for (int i = 5; i >= 0; i--) {
        mac[i] = (uint8_t)mac_value;
        mac_value >>= 8;
    }
    ipv4_init(0, mac, 0, 0);
    arp_init(0, mac);
    icmp_init();
    icmp_set_echo_reply_handler(ping_reply);
    udp_init();
}

static void kernel_configure_network(void) {
    dhcp_lease_t lease;
    if (!dhcp_request(&lease, 100000)) {
        ipv4_set_configuration(0x0A0000E3, 0x0a000002, 0xFFFFFF00);
    }
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
    shell_initialize(&kernel_api);
    terminal_init(&kernel_api);
    notepad_init();
    viewer_init();
    dialog_init(&kernel_api);

    wm_initialize(&kernel_api, kernel_mark_redraw);
    wm_register_window(&notepad_window);
    wm_register_window(&viewer_window);
    wm_register_window(&terminal_window);
    desktop_init(&kernel_api, &terminal_window, &notepad_window);
}

static void kernel_initialize_input(void) {
    debug_print("[INIT] PS/2 Mouse init...\n");
    init_mouse();
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

    debug_print("[INIT] Exec Main Loop.\n");
    while (1) {
        //draw_heartbeat_indicator();
        //net_poll();
        sys_net_poll();

        if ((frame_timer & 0x3F) == 0 && terminal_window.visible && terminal_window.focused) {
            //needs_redraw = 1;
        }

        uint8_t status = inb(0x64);
        if (status & 0x01) {
            if (status & 0x20) {
                uint8_t byte = inb(0x60);

                if (mouse_cycle == 0) {
                    if (byte & 0x08) {
                        mouse_packet[0] = byte;
                        mouse_cycle = 1;
                    }
                } else if (mouse_cycle == 1) {
                    mouse_packet[1] = byte;
                    mouse_cycle = 2;
                } else if (mouse_cycle == 2) {
                    mouse_packet[2] = byte;
                    mouse_cycle = 0;

                    uint8_t flags = mouse_packet[0];
                    int8_t dx = (int8_t)mouse_packet[1];
                    int8_t dy = (int8_t)mouse_packet[2];

                    if (!(flags & 0xC0)) {
                        if (dx != 0 || dy != 0) {
                            mouse_x += dx;
                            mouse_y -= dy;

                            if (mouse_x < 0) mouse_x = 0;
                            if (mouse_x > SCREEN_WIDTH - 12) mouse_x = SCREEN_WIDTH - 12;
                            if (mouse_y < 0) mouse_y = 0;
                            if (mouse_y > SCREEN_HEIGHT - 19) mouse_y = SCREEN_HEIGHT - 19;

                            if (dragging_window) {
                                needs_redraw = 1;
                            } else {
                                update_cursor_fast(mouse_x, mouse_y);
                            }
                        }

                        uint8_t left_click = flags & 0x01;
                        uint8_t click_down = left_click && !prev_left_click;

                        if (left_click != prev_left_click) {
                            needs_redraw = 1;
                        }

                        if (left_click) {
                            if (dragging_window) {
                                dragging_window->x += dx;
                                dragging_window->y -= dy;
                                if (dragging_window->y < 0) dragging_window->y = 0;
                                if (dragging_window->y > TASKBAR_Y - 24) dragging_window->y = TASKBAR_Y - 24;
                                needs_redraw = 1;
                            } else if (click_down) {
                                uint8_t handled = 0;

                                if (mouse_x >= 4 && mouse_x <= 64 &&
                                    mouse_y >= TASKBAR_Y + 4 && mouse_y <= TASKBAR_Y + 28) 
                                {
                                    start_menu_open = !start_menu_open;
                                    handled = 1;
                                    needs_redraw = 1;
                                }
                                else if (mouse_x >= 68 && mouse_x <= 148 &&
                                         mouse_y >= TASKBAR_Y + 4 && mouse_y <= TASKBAR_Y + 28) 
                                {
                                    terminal_window.visible = !terminal_window.visible;
                                    if (terminal_window.visible) wm_bring_to_front(&terminal_window);
                                    start_menu_open = 0;
                                    handled = 1;
                                    needs_redraw = 1;
                                }
                                else if (mouse_x >= 152 && mouse_x <= 232 &&
                                         mouse_y >= TASKBAR_Y + 4 && mouse_y <= TASKBAR_Y + 28) 
                                {
                                    notepad_window.visible = !notepad_window.visible;
                                    if (notepad_window.visible) wm_bring_to_front(&notepad_window);
                                    start_menu_open = 0;
                                    handled = 1;
                                    needs_redraw = 1;
                                }
                                else if (mouse_x >= 236 && mouse_x <= 316 &&
                                         mouse_y >= TASKBAR_Y + 4 && mouse_y <= TASKBAR_Y + 28) 
                                {
                                    viewer_window.visible = !viewer_window.visible;
                                    if (viewer_window.visible) wm_bring_to_front(&viewer_window);
                                    start_menu_open = 0;
                                    handled = 1;
                                    needs_redraw = 1;
                                }
                                else if (wm_get_window(3) && mouse_x >= 320 && mouse_x <= 400 &&
                                         mouse_y >= TASKBAR_Y + 4 && mouse_y <= TASKBAR_Y + 28)
                                {
                                    wm_get_window(3)->visible = !wm_get_window(3)->visible;
                                    if (wm_get_window(3)->visible) wm_bring_to_front(wm_get_window(3));
                                    start_menu_open = 0;
                                    handled = 1;
                                    needs_redraw = 1;
                                }
                                else if (start_menu_open && mouse_x >= 4 && mouse_x <= 124 &&
                                         mouse_y >= (TASKBAR_Y - 96) && mouse_y < TASKBAR_Y) 
                                {
                                    if (mouse_y < TASKBAR_Y - 64) {
                                        terminal_window.visible = 1;
                                        wm_bring_to_front(&terminal_window);
                                    } else if (mouse_y < TASKBAR_Y - 32) {
                                        notepad_window.visible = 1;
                                        wm_bring_to_front(&notepad_window);
                                    } else {
                                        viewer_window.visible = 1;
                                        wm_bring_to_front(&viewer_window);
                                    }
                                    start_menu_open = 0;
                                    handled = 1;
                                    needs_redraw = 1;
                                }

                                if (!handled) {
                                    start_menu_open = 0;
                                    for (int i = WM_MAX_WINDOWS - 1; i >= 0; i--) {
                                        Window* w = wm_get_window(i);
                                        if (!w || !w->visible) continue;

                                        int32_t btn_x = w->x + w->w - 16;
                                        int32_t btn_y = w->y + 3;
                                        if (mouse_x >= btn_x && mouse_x <= btn_x + 13 &&
                                            mouse_y >= btn_y && mouse_y <= btn_y + 13) 
                                        {
                                            w->visible = 0;
                                            handled = 1;
                                            needs_redraw = 1;
                                            break;
                                        }

                                        if (mouse_x >= w->x && mouse_x <= (w->x + w->w) &&
                                            mouse_y >= w->y && mouse_y <= (w->y + 18)) 
                                        {
                                            wm_bring_to_front(w);
                                            dragging_window = w;
                                            handled = 1;
                                            needs_redraw = 1;
                                            break;
                                        }

                                        if (mouse_x >= w->x && mouse_x <= (w->x + w->w) &&
                                            mouse_y >= w->y && mouse_y <= (w->y + w->h)) 
                                        {
                                            wm_bring_to_front(w);
                                            handled = 1;
                                            needs_redraw = 1;
                                            if (w->handle_mouse) {
                                                w->handle_mouse(w, mouse_x - w->x, mouse_y - w->y, left_click);
                                            }
                                            break;
                                        }
                                    }
                                }

                                if (!handled && mouse_y < TASKBAR_Y) {
                                    if (desktop_handle_click(mouse_x, mouse_y, frame_timer)) {
                                        needs_redraw = 1;
                                    }
                                }
                            }
                        } else {
                            dragging_window = 0;
                        }

                        prev_left_click = left_click;
                    }
                }
            } else {
                uint8_t scancode = inb(0x60);
                if (scancode & 0x80) {
                    continue;
                }

                if (scancode == 0x58) {
                    debug_print("[KBD] Triggering Screenshot Capture via Function Key!\n");
                    screenshot_capture();
                    needs_redraw = 1;
                }

                if (scancode == 0x3C || scancode == 0x3D || scancode == 0x48 || scancode == 0x50 || scancode == 0x01) {
                    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
                        Window* window = wm_get_window(i);
                        if (window && window->visible && window->focused && window->handle_key) {
                            window->handle_key(window, (char)scancode);
                            needs_redraw = 1;
                            break;
                        }
                    }
                } else {
                    char ascii = kbd_scancode_to_ascii(scancode);
                    if (ascii != 0) {
                        for (int i = 0; i < WM_MAX_WINDOWS; i++) {
                            Window* window = wm_get_window(i);
                            if (window && window->visible && window->focused && window->handle_key) {
                                window->handle_key(window, ascii);
                                needs_redraw = 1;
                                break;
                            }
                        }
                    }
                }
            }
        }

        if (needs_redraw) {
            render_screen();
            needs_redraw = 0;
        }
    }
}

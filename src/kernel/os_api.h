#ifndef OS_API_H
#define OS_API_H

#include "types.h"
#include "fs.h"
#include "window.h"
#include "../system/graphics/image/png.h"
#include "drivers/net.h"
#include "net/icmp.h"
#include "net/dns.h"

typedef enum {
    APP_TYPE_GUI = 1,
    APP_TYPE_CONSOLE = 2
} app_type_t;

typedef struct {
    uint32_t magic;
    uint8_t type;
    uint8_t reserved[3];
} app_header_t;

#define APP_HEADER_MAGIC 0x31505041
typedef void (*shell_output_fn)(const char* text);
typedef void (*shell_clear_fn)(void);

typedef enum {
    OS_INPUT_NONE = 0,
    OS_INPUT_KEY = 1,
    OS_INPUT_MOUSE = 2
} os_input_type_t;

typedef struct {
    os_input_type_t type;
    uint8_t scancode;
    int8_t mouse_dx;
    int8_t mouse_dy;
    uint8_t mouse_buttons;
} os_input_event_t;

typedef struct {
    void (*fill_rect)(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
    void (*put_pixel)(int32_t x, int32_t y, uint32_t color);
    void (*draw_string)(int32_t x, int32_t y, const char* str, uint32_t color);
    void (*draw_button)(int32_t x, int32_t y, int32_t w, int32_t h, const char* label, uint8_t pressed);
    void (*render_screen)(void);
    uint8_t (*take_redraw_request)(void);
    void (*capture_screenshot)(void);
    void (*blit_rect)(int32_t x, int32_t y, int32_t w, int32_t h);
    void (*blit_all)(void);
    int (*png_decode_and_resize)(const uint8_t* data, uint32_t len,
                                 uint32_t out_pixels[PNG_TARGET_W * PNG_TARGET_H]);
    void (*png_render)(const uint8_t* data, uint32_t len,
                       int32_t x, int32_t y, int32_t w, int32_t h);
} os_graphics_api_t;

typedef struct {
    uint8_t (*ps2_status)(void);
    uint8_t (*ps2_read_data)(void);
    void (*ps2_initialize_mouse)(void);
    int (*poll_input)(os_input_event_t* event);
    char (*translate_scancode)(uint8_t scancode);
    void (*move_cursor)(int32_t x, int32_t y, uint8_t fast_update);
} os_input_api_t;

typedef struct {
    int (*list)(char file_list_out[FS_MAX_FILES][FS_MAX_FILENAME]);
    int (*read)(const char* filename, uint8_t* dest, uint32_t max_bytes);
    int (*write)(const char* filename, const uint8_t* src, uint32_t size_bytes);
    int (*delete_file)(const char* filename);
} os_filesystem_api_t;

typedef struct {
    void (*term_print)(const char* text);
    const char* (*command_args)(void);
    void (*shell_set_output)(shell_output_fn output);
    int (*shell_open_viewer)(const char* filename);
    int (*launch_app)(const char* name, const char* arguments);
} os_shell_api_t;

typedef struct {
    void (*net_poll)(void);
    uint32_t (*get_ticks)(void);
    uint8_t (*icmp_request_echo)(uint32_t destination_ip, uint16_t identifier,
                                 uint16_t sequence, const uint8_t* payload,
                                 uint16_t length);
    void (*icmp_set_echo_reply_handler)(icmp_echo_reply_handler_t handler);
    uint8_t (*net_is_ready)(void);
    void (*net_get_diagnostics)(net_diagnostics_t* diagnostics);
    uint64_t (*get_mac_address)(void);
    void (*get_configuration)(uint32_t* subnet_mask, uint32_t* gateway,
                              uint8_t* dhcp_leased);
    uint8_t (*ipv4_send)(uint32_t dest_ip, uint8_t protocol, const uint8_t* payload, uint32_t length);
    void    (*ipv4_set_handler)(uint8_t protocol, ipv4_protocol_handler_t handler);
    uint32_t (*ipv4_get_local_ip)(void);
    uint8_t (*dns_resolve)(const char* hostname, uint32_t* address);
} os_network_api_t;

typedef struct {
    int  (*register_window)(Window* win);
    void (*unregister_window)(Window* win);
    void (*show_info_dialog)(const char* title, const char* message);
} os_window_api_t;

typedef struct {
    int  (*floppy_init)(void);
    int  (*floppy_read)(uint32_t lba, uint8_t count, uint8_t* buf);
    int  (*floppy_write)(uint32_t lba, uint8_t count, const uint8_t* buf);
} os_device_api_t;

typedef struct {
    void (*shutdown)(void);
    void (*reboot)(void);
} os_system_api_t;

typedef struct {
    os_graphics_api_t graphics;
    os_input_api_t input;
    os_filesystem_api_t fs;
    os_shell_api_t shell;
    os_network_api_t network;
    os_window_api_t windows;
    os_device_api_t devices;
    os_system_api_t system;
} os_api_t;

#endif

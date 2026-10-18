#ifndef OS_API_H
#define OS_API_H

#include "types.h"
#include "fs.h"
#include "window.h"
#include "drivers/net.h"
#include "net/icmp.h"

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

typedef struct {
    // screen draw functions
    void (*fill_rect)(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
    void (*draw_string)(int32_t x, int32_t y, const char* str, uint32_t color);
    void (*draw_button)(int32_t x, int32_t y, int32_t w, int32_t h, const char* label, uint8_t pressed);
    void (*blit_rect)(int32_t x, int32_t y, int32_t w, int32_t h);
    void (*blit_all)(void);
    // ata filesystem functions
    int  (*fs_list)(char file_list_out[FS_MAX_FILES][FS_MAX_FILENAME]);
    int  (*fs_read)(const char* filename, uint8_t* dest, uint32_t max_bytes);
    int  (*fs_write)(const char* filename, const uint8_t* src, uint32_t size_bytes);
    int (*fs_delete)(const char* filename);
    // some terminal shit
    void (*term_print)(const char* text);
    void (*shell_execute)(const char* command, shell_output_fn output,
                          shell_clear_fn clear);
    const char* (*command_args)(void);
    void (*net_poll)(void);
    uint32_t (*get_ticks)(void);
    uint8_t (*icmp_request_echo)(uint32_t destination_ip, uint16_t identifier,
                                 uint16_t sequence, const uint8_t* payload,
                                 uint16_t length);
    void (*icmp_set_echo_reply_handler)(icmp_echo_reply_handler_t handler);
    uint8_t (*net_is_ready)(void);
    void (*net_get_diagnostics)(net_diagnostics_t* diagnostics);
    // window manager functions
    int  (*register_window)(Window* win);
    void (*unregister_window)(Window* win);
    void (*show_info_dialog)(const char* title, const char* message);
    // floppy functions
    int  (*floppy_init)(void);
    int  (*floppy_read)(uint32_t lba, uint8_t count, uint8_t* buf);
    int  (*floppy_write)(uint32_t lba, uint8_t count, const uint8_t* buf);
} os_api_t;

#endif
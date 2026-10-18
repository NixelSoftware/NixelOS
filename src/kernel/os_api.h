#ifndef OS_API_H
#define OS_API_H

#include "types.h"
#include "fs.h"
#include "window.h"

typedef struct {
    
    void (*fill_rect)(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
    void (*draw_string)(int32_t x, int32_t y, const char* str, uint32_t color);
    void (*draw_button)(int32_t x, int32_t y, int32_t w, int32_t h, const char* label, uint8_t pressed);
    void (*blit_rect)(int32_t x, int32_t y, int32_t w, int32_t h);
    void (*blit_all)(void);
    int  (*fs_list)(char file_list_out[FS_MAX_FILES][FS_MAX_FILENAME]);
    int  (*fs_read)(const char* filename, uint8_t* dest, uint32_t max_bytes);
    int  (*fs_write)(const char* filename, const uint8_t* src, uint32_t size_bytes);
    int (*fs_delete)(const char* filename);
    void (*term_print)(const char* text);
    int  (*register_window)(Window* win);
    void (*unregister_window)(Window* win);
    void (*show_info_dialog)(const char* title, const char* message);
} os_api_t;

#endif
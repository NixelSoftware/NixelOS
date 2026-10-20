#include "screenshot.h"
#include "../../../kernel/fs.h"
#include "../../../kernel/drivers/display.h"
#include "../../../kernel/drivers/serial.h"

#pragma pack(push, 1)
typedef struct {
    uint16_t type;
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;
    uint32_t header_size;
    int32_t  width;
    int32_t  height;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t image_size;
    int32_t  x_pixels_per_meter;
    int32_t  y_pixels_per_meter;
    uint32_t colors_used;
    uint32_t colors_important;
} bmp_header_t;
#pragma pack(pop)

#define SCREENSHOT_SCRATCH_ADDR 0x00500000
#define SCREENSHOT_MAX_SIZE     (3 * 1024 * 1024)

static uint8_t screenshot_buffer[SCREENSHOT_MAX_SIZE];

int screenshot_capture(void) {
    const char* filename = "SHOT.BMP";

    uint32_t w = SCREEN_WIDTH;
    uint32_t h = SCREEN_HEIGHT;
    uint32_t* fb = (uint32_t*)display_get_backbuffer();

    if (!fb || w == 0 || h == 0) {
        debug_print("[SCREENSHOT] Framebuffer unavailable.\n");
        return 0;
    }

    uint32_t row_size = ((w * 3) + 3) & ~3;
    uint32_t image_size = row_size * h;
    uint32_t file_size = sizeof(bmp_header_t) + image_size;

    if (file_size > SCREENSHOT_MAX_SIZE) {
        debug_print("[SCREENSHOT] Resolution exceeds supported screenshot limits.\n");
        return 0;
    }

    uint8_t* screenshot_buffer = (uint8_t*)SCREENSHOT_SCRATCH_ADDR;

    
    bmp_header_t* header = (bmp_header_t*)screenshot_buffer;
    header->type = 0x4D42; 
    header->size = file_size;
    header->reserved1 = 0;
    header->reserved2 = 0;
    header->offset = sizeof(bmp_header_t);
    header->header_size = 40;
    header->width = (int32_t)w;
    header->height = (int32_t)h;
    header->planes = 1;
    header->bpp = 24;
    header->compression = 0;
    header->image_size = image_size;
    header->x_pixels_per_meter = 2835;
    header->y_pixels_per_meter = 2835;
    header->colors_used = 0;
    header->colors_important = 0;

    
    uint8_t* pixel_dest = screenshot_buffer + sizeof(bmp_header_t);
    for (int32_t y = (int32_t)h - 1; y >= 0; y--) {
        uint8_t* row = pixel_dest + ((h - 1 - y) * row_size);
        for (uint32_t x = 0; x < w; x++) {
            uint32_t color = fb[y * w + x];
            row[x * 3 + 0] = (uint8_t)(color & 0xFF);         
            row[x * 3 + 1] = (uint8_t)((color >> 8) & 0xFF);  
            row[x * 3 + 2] = (uint8_t)((color >> 16) & 0xFF); 
        }
        for (uint32_t pad = w * 3; pad < row_size; pad++) {
            row[pad] = 0;
        }
    }

    int res = fs_write_file(filename, screenshot_buffer, file_size);
    if (res >= 0) {
        debug_print("[SCREENSHOT] Saved successfully: SHOT.BMP\n");
        return 1;
    }

    debug_print("[SCREENSHOT] Failed to write screenshot.\n");
    return 0;
}
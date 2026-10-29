#include "desktop_wallpaper.h"
#include "../../../kernel/drivers/display.h"
#include "../../../kernel/drivers/serial.h"
#include "../../../kernel/paging.h"

#define SCREEN_W  800
#define SCREEN_H  600
#define TARGET_PAGES ((SCREEN_W * SCREEN_H * 4u + PAGE_SIZE - 1u) / PAGE_SIZE)

static uint32_t* wallpaper_pixels = 0;
static uint32_t  allocated_frames[TARGET_PAGES];
static uint8_t   wallpaper_loaded = 0;
static wallpaper_mode_t current_mode = WALLPAPER_MODE_STRETCH;

static uint8_t   bmp_header_buf[54];
static uint8_t   row_scratch[SCREEN_W * 4];

static int wallpaper_allocate_target(void) {
    if (wallpaper_pixels) return 1;

    for (uint32_t i = 0; i < TARGET_PAGES; i++) {
        uint32_t frame = paging_frame_alloc();
        if (!frame) {
            while (i > 0) paging_frame_free(allocated_frames[--i]);
            return 0;
        }
        allocated_frames[i] = frame;
    }

    wallpaper_pixels = (uint32_t*)allocated_frames[0];
    return 1;
}

void desktop_wallpaper_init(os_api_t* api, wallpaper_mode_t mode) {
    wallpaper_loaded = 0;
    current_mode = mode;
    if (!api || !api->fs.read) return;

    int bytes_read = api->fs.read("WALLPAPR.BMP", bmp_header_buf, sizeof(bmp_header_buf));
    if (bytes_read < 54 || bmp_header_buf[0] != 'B' || bmp_header_buf[1] != 'M') {
        debug_print("[WALLPAPER] WALLPAPR.BMP not found or invalid header.\n");
        return;
    }

    uint32_t data_offset = *(uint32_t*)&bmp_header_buf[0x0A];
    int32_t  img_w       = *(int32_t*)&bmp_header_buf[0x12];
    int32_t  img_h       = *(int32_t*)&bmp_header_buf[0x16];
    uint16_t bpp         = *(uint16_t*)&bmp_header_buf[0x1C];
    uint32_t compression = *(uint32_t*)&bmp_header_buf[0x1E];

    if (compression != 0 || (bpp != 24 && bpp != 32) || img_w <= 0 || img_h == 0) {
        debug_print("[WALLPAPER] Unsupported BMP format.\n");
        return;
    }

    if (!wallpaper_allocate_target()) {
        debug_print("[WALLPAPER] Out of physical memory for wallpaper.\n");
        return;
    }

    int flip_y = 1;
    if (img_h < 0) {
        img_h = -img_h;
        flip_y = 0;
    }

    if (img_w > SCREEN_W) return; /* fixed input row scratch capacity */
    uint32_t row_stride = (bpp == 24) ? (((img_w * 3) + 3) & ~3) : (img_w * 4);

    if (current_mode == WALLPAPER_MODE_STRETCH) {
        int last_fetched_src_y = -1;

        for (int y = 0; y < SCREEN_H; y++) {
            int src_y = (y * img_h) / SCREEN_H;
            if (src_y >= img_h) src_y = img_h - 1;
            int file_row_y = flip_y ? (img_h - 1 - src_y) : src_y;

            if (file_row_y != last_fetched_src_y) {
                uint32_t offset = data_offset + (file_row_y * row_stride);
                fs_read_file_at("WALLPAPR.BMP", row_scratch, row_stride, offset);
                last_fetched_src_y = file_row_y;
            }


            for (int x = 0; x < SCREEN_W; x++) {
                int src_x = (x * img_w) / SCREEN_W;
                if (src_x >= img_w) src_x = img_w - 1;

                uint32_t color;
                if (bpp == 24) {
                    uint8_t b = row_scratch[src_x * 3 + 0];
                    uint8_t g = row_scratch[src_x * 3 + 1];
                    uint8_t r = row_scratch[src_x * 3 + 2];
                    color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                } else {
                    uint8_t b = row_scratch[src_x * 4 + 0];
                    uint8_t g = row_scratch[src_x * 4 + 1];
                    uint8_t r = row_scratch[src_x * 4 + 2];
                    color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                }
                uint32_t pixel = y * SCREEN_W + x;
                uint32_t* page = (uint32_t*)allocated_frames[pixel / (PAGE_SIZE / 4u)];
                page[pixel % (PAGE_SIZE / 4u)] = color;
            }
        }
    }

    else if (current_mode == WALLPAPER_MODE_TILE) {
        int last_fetched_src_y = -1;

        for (int y = 0; y < SCREEN_H; y++) {
            int src_y = y % img_h;
            int file_row_y = flip_y ? (img_h - 1 - src_y) : src_y;

            if (file_row_y != last_fetched_src_y) {
                uint32_t offset = data_offset + (file_row_y * row_stride);
                fs_read_file_at("WALLPAPR.BMP", row_scratch, row_stride, offset);
                last_fetched_src_y = file_row_y;
            }


            for (int x = 0; x < SCREEN_W; x++) {
                int src_x = x % img_w;

                uint32_t color;
                if (bpp == 24) {
                    uint8_t b = row_scratch[src_x * 3 + 0];
                    uint8_t g = row_scratch[src_x * 3 + 1];
                    uint8_t r = row_scratch[src_x * 3 + 2];
                    color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                } else {
                    uint8_t b = row_scratch[src_x * 4 + 0];
                    uint8_t g = row_scratch[src_x * 4 + 1];
                    uint8_t r = row_scratch[src_x * 4 + 2];
                    color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                }
                uint32_t pixel = y * SCREEN_W + x;
                uint32_t* page = (uint32_t*)allocated_frames[pixel / (PAGE_SIZE / 4u)];
                page[pixel % (PAGE_SIZE / 4u)] = color;
            }
        }
    }

    wallpaper_loaded = 1;
    debug_print("[WALLPAPER] Applied wallpaper successfully.\n");
}

void desktop_wallpaper_draw(void) {
    if (!wallpaper_loaded || !wallpaper_pixels) {
        display_clear(COLOR_DESKTOP);
        return;
    }
    uint32_t* dest = display_get_backbuffer();
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        int sy = current_mode == WALLPAPER_MODE_TILE ? y % SCREEN_H : y * SCREEN_H / SCREEN_HEIGHT;
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            int sx = current_mode == WALLPAPER_MODE_TILE ? x % SCREEN_W : x * SCREEN_W / SCREEN_WIDTH;
            uint32_t pixel = sy * SCREEN_W + sx;
            const uint32_t* source = (const uint32_t*)allocated_frames[pixel / (PAGE_SIZE / 4u)];
            *dest++ = source[pixel % (PAGE_SIZE / 4u)];
        }
    }
}

void desktop_wallpaper_set_mode(wallpaper_mode_t mode) {
    current_mode = mode;
}

uint8_t desktop_wallpaper_is_loaded(void) {
    return wallpaper_loaded;
}
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
static char wallpaper_path[FS_MAX_PATH];
static uint8_t   row_scratch[SCREEN_W * 4];
static char selected_name[FS_MAX_FILENAME] = "chess.bmp";
static os_api_t* wallpaper_api;

static int valid_name(const char* name) {
    int n = 0;
    while (name[n] && n < FS_MAX_FILENAME - 1) {
        if (name[n] == '/' || name[n] == '\\') return 0;
        n++;
    }
    return n > 4 && !name[n] && name[0] != '.' && name[n-4] == '.' &&
        (name[n-3] | 32) == 'b' && (name[n-2] | 32) == 'm' && (name[n-1] | 32) == 'p';
}

static void copy_name(char* dst, const char* src) {
    int i = 0; while (src[i] && i < FS_MAX_FILENAME - 1) { dst[i] = src[i]; i++; } dst[i] = 0;
}

const char* desktop_wallpaper_name(void) { return selected_name; }

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
    current_mode = mode;
    wallpaper_loaded = 0;
    copy_name(selected_name, "chess.bmp");
    wallpaper_api = api;
    if (!api || !api->fs.read) return;

    char config[FS_MAX_PATH], name[FS_MAX_FILENAME];
    fs_home_path("/.config/desktop/wallpaper", config);
    int n = fs_read_file(config, (uint8_t*)name, sizeof(name) - 1);
    if (n > 0 && n < FS_MAX_FILENAME) { name[n] = 0; if (valid_name(name)) copy_name(selected_name, name); }
    if (!desktop_wallpaper_select(selected_name)) {
        copy_name(selected_name, "chess.bmp");
        desktop_wallpaper_select(selected_name);
    }
}

int desktop_wallpaper_select(const char* name) {
    if (!wallpaper_api || !valid_name(name)) return 0;
    char suffix[FS_MAX_FILENAME + 21] = "/Pictures/Wallpapers/";
    int j = 21, i = 0;
    while (name[i]) suffix[j++] = name[i++];
    suffix[j] = 0;
    fs_home_path(suffix, wallpaper_path);
    int bytes_read = wallpaper_api->fs.read(wallpaper_path, bmp_header_buf, sizeof(bmp_header_buf));
    if (bytes_read < 54 || bmp_header_buf[0] != 'B' || bmp_header_buf[1] != 'M') {
        debug_print("[WALLPAPER] WALLPAPR.BMP not found or invalid header.\n");
        return 0;
    }

    uint32_t data_offset = *(uint32_t*)&bmp_header_buf[0x0A];
    int32_t  img_w       = *(int32_t*)&bmp_header_buf[0x12];
    int32_t  img_h       = *(int32_t*)&bmp_header_buf[0x16];
    uint16_t bpp         = *(uint16_t*)&bmp_header_buf[0x1C];
    uint32_t compression = *(uint32_t*)&bmp_header_buf[0x1E];

    if (compression != 0 || (bpp != 24 && bpp != 32) || img_w <= 0 || img_h == 0 || img_h < -32767) {
        debug_print("[WALLPAPER] Unsupported BMP format.\n");
        return 0;
    }

    if (!wallpaper_allocate_target()) {
        debug_print("[WALLPAPER] Out of physical memory for wallpaper.\n");
        return 0;
    }

    int flip_y = 1;
    if (img_h < 0) {
        img_h = -img_h;
        flip_y = 0;
    }

    if (img_w > 32767 || img_h > 32767) return 0;
    uint32_t row_stride = (bpp == 24) ? (((img_w * 3) + 3) & ~3) : (img_w * 4);
    uint32_t file_size = (uint32_t)fs_read_file(wallpaper_path, 0, 0);
    uint32_t image_size = row_stride * (uint32_t)img_h;
    if (data_offset > file_size || image_size > file_size - data_offset) return 0;

    if (current_mode == WALLPAPER_MODE_STRETCH) {
        int last_fetched_src_y = -1, cached_chunk = -1;

        for (int y = 0; y < SCREEN_H; y++) {
            int src_y = (y * img_h) / SCREEN_H;
            if (src_y >= img_h) src_y = img_h - 1;
            int file_row_y = flip_y ? (img_h - 1 - src_y) : src_y;

            if (file_row_y != last_fetched_src_y) { cached_chunk = -1; last_fetched_src_y = file_row_y; }


            for (int x = 0; x < SCREEN_W; x++) {
                int src_x = (x * img_w) / SCREEN_W;
                if (src_x >= img_w) src_x = img_w - 1;

                int chunk = src_x / SCREEN_W;
                if (chunk != cached_chunk) {
                    uint32_t bytes_per_pixel = bpp / 8;
                    uint32_t first = (uint32_t)chunk * SCREEN_W;
                    uint32_t pixels = (uint32_t)img_w - first;
                    if (pixels > SCREEN_W) pixels = SCREEN_W;
                    uint32_t size = pixels * bytes_per_pixel;
                    uint32_t offset = data_offset + (uint32_t)file_row_y * row_stride + first * bytes_per_pixel;
                    if (offset < data_offset || fs_read_file_at(wallpaper_path, row_scratch, size, offset) != (int)size) { wallpaper_loaded = 0; return 0; }
                    cached_chunk = chunk;
                }
                src_x %= SCREEN_W;
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
        int last_fetched_src_y = -1, cached_chunk = -1;

        for (int y = 0; y < SCREEN_H; y++) {
            int src_y = y % img_h;
            int file_row_y = flip_y ? (img_h - 1 - src_y) : src_y;

            if (file_row_y != last_fetched_src_y) { cached_chunk = -1; last_fetched_src_y = file_row_y; }


            for (int x = 0; x < SCREEN_W; x++) {
                int src_x = x % img_w;

                int chunk = src_x / SCREEN_W;
                if (chunk != cached_chunk) {
                    uint32_t bytes_per_pixel = bpp / 8;
                    uint32_t first = (uint32_t)chunk * SCREEN_W;
                    uint32_t pixels = (uint32_t)img_w - first;
                    if (pixels > SCREEN_W) pixels = SCREEN_W;
                    uint32_t size = pixels * bytes_per_pixel;
                    uint32_t offset = data_offset + (uint32_t)file_row_y * row_stride + first * bytes_per_pixel;
                    if (offset < data_offset || fs_read_file_at(wallpaper_path, row_scratch, size, offset) != (int)size) { wallpaper_loaded = 0; return 0; }
                    cached_chunk = chunk;
                }
                src_x %= SCREEN_W;
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

    copy_name(selected_name, name);
    char config_path[FS_MAX_PATH];
    fs_home_path("/.config/desktop/wallpaper", config_path);
    int len = 0; while (selected_name[len]) len++;
    int saved = fs_write_file(config_path, (const uint8_t*)selected_name, len) == len;
    wallpaper_loaded = 1;
    debug_print("[WALLPAPER] Applied wallpaper successfully.\n");
    return saved ? 1 : 2;
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
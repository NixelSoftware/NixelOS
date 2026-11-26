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
#define WALLPAPER_CACHE_SIZE 65536u
static uint8_t image_cache[2][WALLPAPER_CACHE_SIZE];
static uint32_t cache_start[2], cache_size[2], image_file_size, cache_next;

/* Two blocks avoid rereading boundaries when bottom-up rows cross them. */
static int read_pixel(uint32_t offset, uint32_t bytes, uint32_t* color) {
    uint8_t pixel[4];
    for (uint32_t i = 0; i < bytes; i++) {
        uint32_t pos = offset + i, slot;
        for (slot = 0; slot < 2; slot++)
            if (pos >= cache_start[slot] && pos - cache_start[slot] < cache_size[slot]) break;
        if (slot == 2) {
            slot = cache_next; cache_next ^= 1;
            cache_start[slot] = pos & ~(WALLPAPER_CACHE_SIZE - 1u);
            cache_size[slot] = image_file_size - cache_start[slot];
            if (cache_size[slot] > WALLPAPER_CACHE_SIZE) cache_size[slot] = WALLPAPER_CACHE_SIZE;
            if (fs_read_file_at(wallpaper_path, image_cache[slot], cache_size[slot], cache_start[slot]) != (int)cache_size[slot]) {
                cache_size[slot] = 0;
                return 0;
            }
        }
        pixel[i] = image_cache[slot][pos - cache_start[slot]];
    }
    *color = ((uint32_t)pixel[2] << 16) | ((uint32_t)pixel[1] << 8) | pixel[0];
    return 1;
}
static char selected_name[FS_MAX_FILENAME] = "chess.bmp";
static os_api_t* wallpaper_api;
static int load_wallpaper(const char* name, int persist);

int desktop_wallpaper_valid_name(const char* name) {
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
    if (n > 0 && n < FS_MAX_FILENAME) { name[n] = 0; if (desktop_wallpaper_valid_name(name)) copy_name(selected_name, name); }
    if (!load_wallpaper(selected_name, 0)) {
        copy_name(selected_name, "chess.bmp");
        load_wallpaper(selected_name, 0);
    }
}

int desktop_wallpaper_select(const char* name) {
    return load_wallpaper(name, 1);
}

static int load_wallpaper(const char* name, int persist) {
    if (!wallpaper_api || !desktop_wallpaper_valid_name(name)) return 0;
    char suffix[sizeof(WALLPAPER_USER_DIR) + FS_MAX_FILENAME] = WALLPAPER_USER_DIR "/";
    copy_name(suffix + sizeof(WALLPAPER_USER_DIR), name);
    int bytes_read = -1;
    if (fs_home_path(suffix, wallpaper_path))
        bytes_read = wallpaper_api->fs.read(wallpaper_path, bmp_header_buf, sizeof(bmp_header_buf));
    if (bytes_read < 0) {
        const char shared[] = WALLPAPER_SHARED_DIR "/";
        for (unsigned i = 0; i < sizeof(shared) - 1; i++) wallpaper_path[i] = shared[i];
        copy_name(wallpaper_path + sizeof(shared) - 1, name);
        bytes_read = wallpaper_api->fs.read(wallpaper_path, bmp_header_buf, sizeof(bmp_header_buf));
    }
    if (bytes_read < 54 || bmp_header_buf[0] != 'B' || bmp_header_buf[1] != 'M') {
        debug_print("[WALLPAPER] Wallpaper not found or invalid header.\n");
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

    image_file_size = file_size;
    cache_size[0] = cache_size[1] = cache_next = 0;
    uint32_t bytes_per_pixel = bpp / 8;
    for (int y = 0; y < SCREEN_H; y++) {
        int src_y = current_mode == WALLPAPER_MODE_TILE ? y % img_h : (y * img_h) / SCREEN_H;
        int file_row_y = flip_y ? img_h - 1 - src_y : src_y;
        uint32_t row_offset = data_offset + (uint32_t)file_row_y * row_stride;
        for (int x = 0; x < SCREEN_W; x++) {
            int src_x = current_mode == WALLPAPER_MODE_TILE ? x % img_w : (x * img_w) / SCREEN_W;
            uint32_t color;
            if (!read_pixel(row_offset + (uint32_t)src_x * bytes_per_pixel, bytes_per_pixel, &color)) {
                wallpaper_loaded = 0;
                return 0;
            }
            uint32_t pixel = y * SCREEN_W + x;
            uint32_t* page = (uint32_t*)allocated_frames[pixel / (PAGE_SIZE / 4u)];
            page[pixel % (PAGE_SIZE / 4u)] = color;
        }
    }

    copy_name(selected_name, name);
    char config_path[FS_MAX_PATH];
    fs_home_path("/.config/desktop/wallpaper", config_path);
    int len = 0; while (selected_name[len]) len++;
    int saved = !persist || fs_write_file(config_path, (const uint8_t*)selected_name, len) == len;
    wallpaper_loaded = 1;
    debug_print("[WALLPAPER] Applied wallpaper successfully.\n");
    return saved ? 1 : 2;
}

void desktop_wallpaper_draw(void) {
    if (!wallpaper_loaded || !wallpaper_pixels) {
        display_clear(COLOR_DESKTOP);
        return;
    }
    uint32_t* buffer = display_get_backbuffer();
    text_rect_t clip = text_rect_intersect(display_clip(),
        (text_rect_t){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT});
    for (int y = clip.top; y < clip.bottom; y++) {
        uint32_t* dest = buffer + y * SCREEN_WIDTH + clip.left;
        int sy = current_mode == WALLPAPER_MODE_TILE ? y % SCREEN_H : y * SCREEN_H / SCREEN_HEIGHT;
        for (int x = clip.left; x < clip.right; x++) {
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

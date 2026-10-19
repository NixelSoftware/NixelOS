#include "viewer.h"
#include "../../kernel/drivers/display/font.h"
#include "../../kernel/drivers/display.h"
#include "../../kernel/fs.h"

Window viewer_window;

#define MAX_BMP_W 200
#define MAX_BMP_H 150
//#define BMP_MAX_FILE_SIZE (16 * 1024) 
#define BMP_MAX_FILE_SIZE 1024000

static uint32_t image_pixels[MAX_BMP_W * MAX_BMP_H];
static int32_t  image_w = 0;
static int32_t  image_h = 0;
static char     loaded_filename[FS_MAX_FILENAME] = "";
static uint8_t  bmp_raw_buffer[BMP_MAX_FILE_SIZE];

int viewer_load_bmp(const char* filename) {
    int bytes = fs_read_file(filename, bmp_raw_buffer, sizeof(bmp_raw_buffer));
    if (bytes < 54) {
        
        return 0;
    }

    
    if (bmp_raw_buffer[0] != 'B' || bmp_raw_buffer[1] != 'M') {
        return 0;
    }

    
    uint32_t data_offset = *(uint32_t*)&bmp_raw_buffer[0x0A];
    int32_t  w           = *(int32_t*)&bmp_raw_buffer[0x12];
    int32_t  h           = *(int32_t*)&bmp_raw_buffer[0x16];
    uint16_t bpp         = *(uint16_t*)&bmp_raw_buffer[0x1C];
    uint32_t compression = *(uint32_t*)&bmp_raw_buffer[0x1E];

    
    if (compression != 0 || (bpp != 24 && bpp != 32)) {
        return 0;
    }

    
    int flip_y = 1;
    if (h < 0) {
        h = -h;
        flip_y = 0; 
    }

    if (w <= 0 || h <= 0) return 0;

    
    int32_t draw_w = (w > MAX_BMP_W) ? MAX_BMP_W : w;
    int32_t draw_h = (h > MAX_BMP_H) ? MAX_BMP_H : h;

    image_w = draw_w;
    image_h = draw_h;

    
    uint32_t row_stride;
    if (bpp == 24) {
        row_stride = ((w * 3) + 3) & ~3;
    } else {
        row_stride = w * 4;
    }

    for (int y = 0; y < draw_h; y++) {
        int src_y = flip_y ? (h - 1 - y) : y;
        const uint8_t* row_ptr = &bmp_raw_buffer[data_offset + (src_y * row_stride)];

        for (int x = 0; x < draw_w; x++) {
            uint32_t color = 0;
            if (bpp == 24) {
                uint8_t b = row_ptr[x * 3 + 0];
                uint8_t g = row_ptr[x * 3 + 1];
                uint8_t r = row_ptr[x * 3 + 2];
                color = (r << 16) | (g << 8) | b;
            } else if (bpp == 32) {
                uint8_t b = row_ptr[x * 4 + 0];
                uint8_t g = row_ptr[x * 4 + 1];
                uint8_t r = row_ptr[x * 4 + 2];
                color = (r << 16) | (g << 8) | b;
            }
            image_pixels[y * MAX_BMP_W + x] = color;
        }
    }

    
    int i = 0;
    while (filename[i] && i < FS_MAX_FILENAME - 1) {
        loaded_filename[i] = filename[i];
        i++;
    }
    loaded_filename[i] = '\0';

    return 1;
}

void viewer_draw(Window* win) {
    int32_t box_x = win->x + 6;
    int32_t box_y = win->y + 20;
    int32_t box_w = win->w - 12;
    int32_t box_h = win->h - 26;

    
    display_fill_rect(box_x, box_y, box_w, box_h, COLOR_DARK_GRAY);
    display_fill_rect(box_x - 1, box_y - 1, box_w + 2, 1, COLOR_DARK_GRAY);
    display_fill_rect(box_x - 1, box_y - 1, 1, box_h + 2, COLOR_DARK_GRAY);
    display_fill_rect(box_x - 1, box_y + box_h, box_w + 2, 1, COLOR_WHITE);
    display_fill_rect(box_x + box_w, box_y - 1, 1, box_h + 2, COLOR_WHITE);

    if (image_w > 0 && image_h > 0) {
        
        int32_t start_x = box_x + (box_w - image_w) / 2;
        int32_t start_y = box_y + (box_h - image_h) / 2;

        uint32_t* buf = display_get_backbuffer();
        for (int y = 0; y < image_h; y++) {
            int py = start_y + y;
            if (py < 0 || py >= SCREEN_HEIGHT) continue;

            for (int x = 0; x < image_w; x++) {
                int px = start_x + x;
                if (px < 0 || px >= SCREEN_WIDTH) continue;

                buf[py * SCREEN_WIDTH + px] = image_pixels[y * MAX_BMP_W + x];
            }
        }
    } else {
        draw_string(box_x + 12, box_y + (box_h / 2) - 4, "No image loaded", COLOR_WHITE);
        draw_string(box_x + 12, box_y + (box_h / 2) + 8, "Use terminal: view <file>", COLOR_DARK_GRAY);
    }
}

void viewer_init(void) {
    viewer_window.x = 420;
    viewer_window.y = 80;
    viewer_window.w = 240;
    viewer_window.h = 200;
    viewer_window.title = "Image Viewer";
    viewer_window.visible = 1;
    viewer_window.focused = 0;
    viewer_window.draw_content = viewer_draw;
    viewer_window.handle_key = 0;

    image_w = 0;
    image_h = 0;
    loaded_filename[0] = '\0';

    
    viewer_load_bmp("image.bmp");
}
#include "png.h"
#include "../../../kernel/drivers/display.h"

#include "../../../apps/nixplorer/engine/paint/png.c"

void png_render(const uint8_t* data, uint32_t len,
                int32_t x, int32_t y, int32_t w, int32_t h) {
    static uint32_t pixels[PNG_TARGET_W * PNG_TARGET_H];

    if (w <= 0 || h <= 0 ||
        !png_decode_and_resize(data, len, pixels)) {
        return;
    }

    for (int32_t dest_y = 0; dest_y < h; dest_y++) {
        int32_t source_y = (dest_y * PNG_TARGET_H) / h;
        for (int32_t dest_x = 0; dest_x < w; dest_x++) {
            int32_t source_x = (dest_x * PNG_TARGET_W) / w;
            display_put_pixel(x + dest_x, y + dest_y,
                              pixels[source_y * PNG_TARGET_W + source_x]);
        }
    }
}
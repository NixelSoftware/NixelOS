#include "video_drivers.h"

int vga_video_init(video_framebuffer_t* fb, uint32_t address,
                   uint32_t bpp, uint32_t pitch) {
    if (!address || (bpp != 24 && bpp != 32)) return 0;
    uint32_t row_bytes = SCREEN_WIDTH * (bpp / 8);
    if (!pitch) pitch = row_bytes;
    if (pitch < row_bytes || pitch > (0xFFFFFFFFu - address) / SCREEN_HEIGHT)
        return 0;
    *fb = (video_framebuffer_t){address, bpp, pitch, "VGA/VBE"};
    return 1;
}

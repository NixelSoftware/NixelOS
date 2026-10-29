#include "video.h"
#include "../drivers/display/video_drivers.h"
#include "../drivers/serial.h"

static video_framebuffer_t framebuffer;
int video_init(uint32_t address, uint32_t bpp, uint32_t pitch) {
    framebuffer = (video_framebuffer_t){0};
    if (!qemu_video_init(&framebuffer) &&
        !vga_video_init(&framebuffer, address, bpp, pitch)) {
        debug_print("[VIDEO] No supported framebuffer\n");
        return 0;
    }
    debug_print("[VIDEO] Driver: ");
    debug_print(framebuffer.driver_name);
    debug_print("\n");
    return 1;
}
const video_framebuffer_t* video_get_framebuffer(void) {
    return framebuffer.address ? &framebuffer : 0;
}
void video_write_span(int32_t x, int32_t y, const uint32_t* pixels, uint32_t count) {
    if (!framebuffer.address || !pixels || y < 0 || y >= SCREEN_HEIGHT) return;
    if (x < 0) {
        uint32_t skip = 0u - (uint32_t)x;
        if (skip >= count) return;
        pixels += skip; count -= skip; x = 0;
    }
    if (x >= SCREEN_WIDTH) return;
    if (count > SCREEN_WIDTH - (uint32_t)x) count = SCREEN_WIDTH - (uint32_t)x;
    uint8_t* dest = (uint8_t*)framebuffer.address + y * framebuffer.pitch +
                    x * (framebuffer.bpp / 8);
    if (framebuffer.bpp == 32) {
        __asm__ volatile ("cld; rep movsl" : "+D"(dest), "+S"(pixels), "+c"(count) : : "memory");
    } else {
        volatile uint8_t* out = dest;
        while (count--) {
            uint32_t color = *pixels++;
            *out++ = color; *out++ = color >> 8; *out++ = color >> 16;
        }
    }
}

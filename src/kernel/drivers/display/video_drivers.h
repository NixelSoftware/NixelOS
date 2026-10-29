#ifndef NIXEL_VIDEO_DRIVERS_H
#define NIXEL_VIDEO_DRIVERS_H
#include "../../hal/video.h"
int qemu_video_init(video_framebuffer_t* framebuffer);
int vga_video_init(video_framebuffer_t* framebuffer, uint32_t address,
                   uint32_t bpp, uint32_t pitch);
#endif

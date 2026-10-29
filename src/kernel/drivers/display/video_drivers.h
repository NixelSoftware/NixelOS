#ifndef NIXEL_VIDEO_DRIVERS_H
#define NIXEL_VIDEO_DRIVERS_H
#include "../../hal/video.h"
int virtio_gpu_init(video_framebuffer_t* framebuffer);
int virtio_gpu_poll_resize(video_framebuffer_t* framebuffer);
void virtio_gpu_present(void);
void virtio_gpu_mark_dirty(int32_t x, int32_t y, uint32_t count);
int qemu_video_init(video_framebuffer_t* framebuffer);
int vga_video_init(video_framebuffer_t* framebuffer, uint32_t address,
                   uint32_t bpp, uint32_t pitch);
#endif

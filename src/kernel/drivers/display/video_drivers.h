#ifndef NIXEL_VIDEO_DRIVERS_H
#define NIXEL_VIDEO_DRIVERS_H
#include "../../hal/video.h"
int virtio_gpu_init(video_framebuffer_t* framebuffer);
int virtio_gpu_poll_resize(video_framebuffer_t* framebuffer);
void virtio_gpu_present(void);
void virtio_gpu_mark_dirty(int32_t x, int32_t y, uint32_t count);
int virtio_gpu_set_resolution(video_framebuffer_t* fb, uint32_t width, uint32_t height);
int qemu_video_set_resolution(video_framebuffer_t* fb, uint32_t width, uint32_t height);
int intel_gma_can_set_resolution(void);
int intel_gma_native_resolution(uint32_t* width, uint32_t* height);
int intel_gma_set_resolution(video_framebuffer_t* fb, uint32_t width, uint32_t height);
int intel_gma_video_init(video_framebuffer_t* fb, uint32_t address,
                          uint32_t bpp, uint32_t pitch);
int qemu_video_init(video_framebuffer_t* framebuffer);
int vga_video_init(video_framebuffer_t* framebuffer, uint32_t address,
                   uint32_t bpp, uint32_t pitch);
#endif

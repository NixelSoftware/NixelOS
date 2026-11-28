#ifndef BOARD_VIRT_VIRTIO_GPU_H
#define BOARD_VIRT_VIRTIO_GPU_H

#include "types.h"

int virtio_gpu_init(uint32_t width, uint32_t height);
uint32_t* virtio_gpu_framebuffer(void);
uint32_t virtio_gpu_width(void);
uint32_t virtio_gpu_height(void);
void virtio_gpu_present(void);

#endif

#ifndef BOARD_VIRT_VIRTIO_BLK_H
#define BOARD_VIRT_VIRTIO_BLK_H

#include "types.h"

int virtio_blk_init(void);
int virtio_blk_ready(void);
int virtio_blk_read(uint64_t sector, void* buffer, uint32_t count);
int virtio_blk_write(uint64_t sector, const void* buffer, uint32_t count);
uint64_t virtio_blk_sectors(void);

#endif

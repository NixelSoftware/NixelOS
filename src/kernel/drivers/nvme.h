#ifndef KERNEL_NVME_H
#define KERNEL_NVME_H

#include "../types.h"

int  nvme_init(void);
int  nvme_is_ready(void);
int  nvme_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer);
int  nvme_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer);
uint32_t nvme_get_sector_count(void);

#endif
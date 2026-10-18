#ifndef FLOPPY_H
#define FLOPPY_H

#include "../types.h"

int floppy_init(void);
int floppy_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer);
int floppy_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer);

#endif
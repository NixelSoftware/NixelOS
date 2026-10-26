#ifndef ATA_H
#define ATA_H

#include "../types.h"

/* First four: legacy primary/secondary master/slave; then PCI native channels. */
#define ATA_DRIVE_COUNT 32u
int ata_select_drive(unsigned drive);

int ata_read_sectors(uint32_t lba, uint8_t sector_count, uint8_t* buffer);
int ata_write_sectors(uint32_t lba, uint8_t sector_count, const uint8_t* buffer);

#endif
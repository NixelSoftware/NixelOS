#include "ata.h"

#define ATA_DATA         0x1F0
#define ATA_FEATURES     0x1F1
#define ATA_SECTOR_COUNT 0x1F2
#define ATA_LBA_LOW      0x1F3
#define ATA_LBA_MID      0x1F4
#define ATA_LBA_HIGH     0x1F5
#define ATA_DRIVE_SELECT 0x1F6
#define ATA_COMMAND      0x1F7
#define ATA_STATUS       0x1F7

#define ATA_STATUS_BSY  0x80
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_ERR  0x01
#define ATA_STATUS_DF   0x20

#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30

static int ata_wait_ready(void) {
    for (volatile int i = 0; i < 100000; i++) {
        uint8_t status = inb(ATA_STATUS);
        if (!(status & ATA_STATUS_BSY)) {
            return 1;
        }
    }
    return 0;
}

static int ata_wait_drq(void) {
    for (volatile int i = 0; i < 100000; i++) {
        uint8_t status = inb(ATA_STATUS);
        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) return 0;
        if (!(status & ATA_STATUS_BSY) && (status & ATA_STATUS_DRQ)) {
            return 1;
        }
    }
    return 0;
}

static void ata_delay_400ns(void) {
    inb(ATA_STATUS);
    inb(ATA_STATUS);
    inb(ATA_STATUS);
    inb(ATA_STATUS);
}

int ata_read_sectors(uint32_t lba, uint8_t sector_count, uint8_t* buffer) {
    if (!ata_wait_ready()) return 0;

    outb(ATA_DRIVE_SELECT, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_FEATURES, 0x00);
    outb(ATA_SECTOR_COUNT, sector_count);
    outb(ATA_LBA_LOW,  (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND,  ATA_CMD_READ_PIO);

    uint16_t* ptr = (uint16_t*)buffer;
    for (int s = 0; s < sector_count; s++) {
        ata_delay_400ns();
        if (!ata_wait_drq()) return 0;

        for (int i = 0; i < 256; i++) {
            *ptr++ = inw(ATA_DATA);
        }
    }
    return 1;
}

int ata_write_sectors(uint32_t lba, uint8_t sector_count, const uint8_t* buffer) {
    if (!ata_wait_ready()) return 0;

    outb(ATA_DRIVE_SELECT, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_FEATURES, 0x00);
    outb(ATA_SECTOR_COUNT, sector_count);
    outb(ATA_LBA_LOW,  (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND,  ATA_CMD_WRITE_PIO);

    const uint16_t* ptr = (const uint16_t*)buffer;
    for (int s = 0; s < sector_count; s++) {
        ata_delay_400ns();
        if (!ata_wait_drq()) return 0;

        for (int i = 0; i < 256; i++) {
            outw(ATA_DATA, *ptr++);
        }
    }

    
    outb(ATA_COMMAND, 0xE7);
    ata_wait_ready();
    return 1;
}
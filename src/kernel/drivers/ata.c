#include "../mutex.h"
#include "ata.h"
#include "serial.h"

/* Keep legacy positions first, then append PCI native IDE channels. */
typedef struct { uint16_t base, control; } ata_channel_t;
static ata_channel_t channels[ATA_DRIVE_COUNT / 2] = {
    {0x1F0, 0x3F6}, {0x170, 0x376}
};
static unsigned channel_count = 2;
static int controllers_scanned;
static uint8_t drive_state[ATA_DRIVE_COUNT];
static uint32_t drive_sectors[ATA_DRIVE_COUNT];
static unsigned ata_drive;

static uint32_t irq_save(void) {
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}
static void irq_restore(uint32_t flags) {
    __asm__ volatile ("pushl %0; popfl" : : "r"(flags) : "memory", "cc");
}
static uint32_t pci_read(uint32_t address) {
    outl(0xCF8, address);
    return inl(0xCFC);
}
static void ata_find_controllers(void) {
    if (controllers_scanned) return;
    controllers_scanned = 1;
    unsigned ide = 0, ahci = 0;
    for (unsigned bus = 0; bus < 256; bus++) {
        for (unsigned slot = 0; slot < 32; slot++) {
            for (unsigned fn = 0; fn < 8; fn++) {
                uint32_t address = 0x80000000u | (bus << 16) | (slot << 11) | (fn << 8);
                uint32_t flags = irq_save();
                uint32_t id = pci_read(address);
                if ((id & 0xFFFF) == 0xFFFF) { irq_restore(flags); continue; }
                uint32_t cls = pci_read(address | 8);
                if ((cls >> 16) == 0x0106) ahci++;
                if ((cls >> 16) != 0x0101) { irq_restore(flags); continue; }
                ide++;
                uint8_t interface = cls >> 8;
                for (unsigned ch = 0; ch < 2; ch++) {
                    if (!(interface & (1u << (ch * 2)))) continue;
                    uint32_t bar = pci_read(address | (0x10 + ch * 8));
                    uint32_t ctl = pci_read(address | (0x14 + ch * 8));
                    if (!(bar & 1) || !(ctl & 1) || (bar & ~3u) == 0 ||
                        (ctl & ~3u) == 0 || bar > 0xFFFF || ctl > 0xFFFD) continue;
                    uint16_t base = bar & ~3u, control = (ctl & ~3u) + 2;
                    unsigned i = 0;
                    while (i < channel_count && channels[i].base != base) i++;
                    if (i == channel_count && channel_count < ATA_DRIVE_COUNT / 2)
                        channels[channel_count++] = (ata_channel_t){base, control};
                }
                uint16_t command = pci_read(address | 4);
                outl(0xCF8, address | 4);
                /* A 16-bit write avoids clearing PCI status W1C bits. */
                outw(0xCFC, command | 1);
                irq_restore(flags);
            }
        }
    }
    debug_print("[ATA] IDE channels: "); debug_hex(channel_count); debug_print("\n");
    if (!ide && ahci) debug_print("[ATA] AHCI controller detected; AHCI is not supported by this PIO driver.\n");
}
static uint16_t ata_base;
static uint16_t ata_control;
static uint8_t ata_device;
static int ata_selected;

#define ATA_DATA         (ata_base + 0)
#define ATA_FEATURES     (ata_base + 1)
#define ATA_SECTOR_COUNT (ata_base + 2)
#define ATA_LBA_LOW      (ata_base + 3)
#define ATA_LBA_MID      (ata_base + 4)
#define ATA_LBA_HIGH     (ata_base + 5)
#define ATA_DRIVE_SELECT (ata_base + 6)
#define ATA_COMMAND      (ata_base + 7)
#define ATA_STATUS       (ata_base + 7)

#define ATA_STATUS_BSY  0x80
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_ERR  0x01
#define ATA_STATUS_DF   0x20

#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30

/* Early boot has IRQs disabled and the PIT in mode 3 at 100 Hz.
 * Accumulate its latched phase (mode 3 decrements by two) instead of using
 * a CPU-speed-dependent loop. After scheduler startup use its clock. */
static uint32_t ata_pit_count(void) {
    uint32_t flags = irq_save();
    outb(0x43, 0);
    uint32_t count = inb(0x40);
    count |= (uint32_t)inb(0x40) << 8;
    irq_restore(flags);
    return count;
}
static uint64_t ata_now(void) {
    uint32_t flags = irq_save();
    uint64_t now = scheduler_clock();
    irq_restore(flags);
    return now;
}
/* phase: 0=ready for I/O, 1=data, 2=completion, 3=idle for IDENTIFY.
 * Old ERR is ignored before a new command; ATAPI need not assert DRDY. */
static int ata_wait(unsigned phase, uint32_t timeout_ms) {
    int running = scheduler_running();
    uint64_t start = running ? ata_now() : 0;
    uint32_t previous = running ? 0 : ata_pit_count(), elapsed = 0;
    uint32_t budget = timeout_ms * 2387u;
    for (;;) {
        uint8_t status = inb(ATA_STATUS);
        if (status == 0xFF) return 0;
        if (!(status & ATA_STATUS_BSY)) {
            if ((phase == 1 || phase == 2) &&
                (status & (ATA_STATUS_ERR | ATA_STATUS_DF))) return 0;
            if (phase == 1) {
                if (status & ATA_STATUS_DRQ) return 1;
            } else if (!(status & ATA_STATUS_DRQ) && status &&
                       (phase == 2 || phase == 3 || (status & 0x40))) return 1;
        }
        if (running) {
            if (ata_now() - start >= (uint64_t)timeout_ms * 1000000u) break;
            task_sleep_ms(1);
        } else {
            uint32_t count = ata_pit_count();
            if (count <= 11931 && previous <= 11931)
                elapsed += previous >= count ? previous - count : previous + 11931 - count;
            previous = count;
            if (elapsed >= budget) break;
        }
    }
    debug_print("[ATA] Timeout, status: "); debug_hex(inb(ATA_STATUS)); debug_print("\n");
    return 0;
}

static void ata_delay_400ns(void) {
    inb(ata_control);
    inb(ata_control);
    inb(ata_control);
    inb(ata_control);
}

int ata_select_drive(unsigned drive) {
    MUTEX_GUARD(filesystem_mutex);
    ata_selected = 0;
    if (drive >= ATA_DRIVE_COUNT) return 0;
    ata_find_controllers();
    if (drive / 2 >= channel_count || drive_state[drive] == 2) return 0;
    ata_base = channels[drive / 2].base;
    ata_control = channels[drive / 2].control;
    ata_device = (drive & 1) << 4;
    ata_drive = drive;
    /* This is a polling driver; do not leave disk IRQs pending. */
    outb(ata_control, 2);
    outb(ATA_DRIVE_SELECT, 0xA0 | ata_device);
    ata_delay_400ns();
    uint8_t status = inb(ATA_STATUS);
    if (status == 0 || status == 0xFF) return 0;
    if (!ata_wait(3, 30000)) return 0;
    if (!drive_state[drive]) {
        uint16_t identify[256];
        outb(ATA_SECTOR_COUNT, 0);
        outb(ATA_LBA_LOW, 0);
        outb(ATA_LBA_MID, 0);
        outb(ATA_LBA_HIGH, 0);
        outb(ATA_COMMAND, 0xEC); /* IDENTIFY DEVICE, rejects ATAPI devices. */
        ata_delay_400ns();
        if (!ata_wait(1, 30000)) { drive_state[drive] = 2; return 0; }
        for (unsigned i = 0; i < 256; i++) identify[i] = inw(ATA_DATA);
        ata_delay_400ns();
        if (!ata_wait(2, 30000) || !(identify[49] & (1u << 9)) ||
            ((identify[106] & 0xD000) == 0x5000 &&
             (identify[117] != 256 || identify[118]))) {
            drive_state[drive] = 2;
            debug_print("[ATA] Unsupported disk (requires LBA and 512-byte sectors).\n");
            return 0;
        }
        drive_sectors[drive] = identify[60] | ((uint32_t)identify[61] << 16);
        if (!drive_sectors[drive]) { drive_state[drive] = 2; return 0; }
        drive_state[drive] = 1;
        debug_print("[ATA] Disk "); debug_hex(drive);
        debug_print(" at I/O "); debug_hex(ata_base); debug_print("\n");
    }
    ata_selected = 1;
    return 1;
}

static int ata_prepare(uint32_t lba, uint8_t sector_count) {
    if (!ata_selected || !sector_count || lba > 0x0FFFFFFFu ||
        sector_count - 1u > 0x0FFFFFFFu - lba ||
        lba >= drive_sectors[ata_drive] ||
        sector_count > drive_sectors[ata_drive] - lba) return 0;
    outb(ATA_DRIVE_SELECT, 0xE0 | ata_device | ((lba >> 24) & 0x0F));
    ata_delay_400ns();
    return ata_wait(0, 30000);
}

int ata_read_sectors(uint32_t lba, uint8_t sector_count, uint8_t* buffer) {
    MUTEX_GUARD(filesystem_mutex);
    if (!ata_prepare(lba, sector_count)) return 0;
    outb(ATA_FEATURES, 0x00);
    outb(ATA_SECTOR_COUNT, sector_count);
    outb(ATA_LBA_LOW,  (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND,  ATA_CMD_READ_PIO);

    uint16_t* ptr = (uint16_t*)buffer;
    for (int s = 0; s < sector_count; s++) {
        ata_delay_400ns();
        if (!ata_wait(1, 30000)) return 0;

        uint32_t words = 256;
        __asm__ volatile ("cld; rep insw" : "+D"(ptr), "+c"(words) : "d"((uint16_t)ATA_DATA) : "memory");
    }
    ata_delay_400ns();
    return ata_wait(2, 30000);
}

int ata_write_sectors(uint32_t lba, uint8_t sector_count, const uint8_t* buffer) {
    MUTEX_GUARD(filesystem_mutex);
    if (!ata_prepare(lba, sector_count)) return 0;
    outb(ATA_FEATURES, 0x00);
    outb(ATA_SECTOR_COUNT, sector_count);
    outb(ATA_LBA_LOW,  (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND,  ATA_CMD_WRITE_PIO);

    const uint16_t* ptr = (const uint16_t*)buffer;
    for (int s = 0; s < sector_count; s++) {
        ata_delay_400ns();
        if (!ata_wait(1, 30000)) return 0;

        for (unsigned i = 0; i < 256; i++) outw(ATA_DATA, *ptr++);
    }

    
    ata_delay_400ns();
    if (!ata_wait(2, 30000)) return 0;
    outb(ATA_COMMAND, 0xE7);
    ata_delay_400ns();
    return ata_wait(2, 30000);
}
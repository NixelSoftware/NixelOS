/* SPDX-License-Identifier: GPL-2.0-or-later
 * Native adaptation of b43_upload_microcode and SHM access in Linux b43/main.c.
 * Copyright Linux b43 contributors: Michael Buesch, Stefano Brivio,
 * Martin Langer, Danny van Dyk and Andreas Jaggi.
 */
#include "b43_boot.h"

#define MACCTL 0x120
#define IRQ_REASON 0x128
#define SHM_CONTROL 0x160
#define SHM_DATA 0x164
#define SHM_DATA_HIGH 0x166
#define MAC_ENABLED 0x00000001u
#define PSM_RUN 0x00000002u
#define PSM_JMP0 0x00000004u
#define SHM_ENABLED 0x00000100u
#define IHR_ENABLED 0x00000400u
#define MAC_BIG_ENDIAN 0x00010000u
#define IRQ_SUSPENDED 1u
#define SHM_UCODE 0u
#define SHM_SHARED 1u
#define SHM_SCRATCH 2u
#define SHM_HW 3u
#define SHM_AUTOINC_W 0x0100u

static void control(const b43_io_t* io, uint16_t route, uint16_t word) {
    io->write32(io->context, SHM_CONTROL, (uint32_t)route << 16 | word);
}
static void shm_write16(const b43_io_t* io, uint16_t route,
                         uint16_t offset, uint16_t value) {
    uint16_t reg = SHM_DATA;
    if (route == SHM_SHARED) {
        if (offset & 2) reg = SHM_DATA_HIGH;
        offset >>= 2;
    }
    control(io, route, offset);
    io->write16(io->context, reg, value);
}
static uint16_t shared_read16(const b43_io_t* io, uint16_t offset) {
    control(io, SHM_SHARED, offset >> 2);
    return io->read16(io->context, offset & 2 ? SHM_DATA_HIGH : SHM_DATA);
}
static void upload(const b43_io_t* io, const b43_firmware_t* fw) {
    for (uint32_t i = 0; i < fw->length; i += 4) {
        const uint8_t* p = fw->data + i;
        uint32_t word = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
                        (uint32_t)p[2] << 8 | p[3];
        io->write32(io->context, SHM_DATA, word);
        io->delay_us(io->context, 10);
    }
}
int b43_firmware_boot(const b43_io_t* io,
                      const uint8_t* ucode, uint32_t ucode_length,
                      const uint8_t* pcm, uint32_t pcm_length,
                      b43_boot_info_t* info) {
    b43_firmware_t microcode, pcmcode;
    if (!info) return B43_BOOT_INVALID;
    *info = (b43_boot_info_t){0};
    /* Validate both complete files before touching any register. */
    if (!io || !io->read16 || !io->read32 || !io->write16 ||
        !io->write32 || !io->delay_us ||
        !b43_firmware_parse(ucode, ucode_length, 'u', &microcode) ||
        microcode.length > 0x40000u || /* 16-bit SHM word address */
        (!pcm && pcm_length) || (pcm && !b43_firmware_parse(pcm, pcm_length, 'p', &pcmcode)))
        return B43_BOOT_INVALID;
    uint32_t macctl = io->read32(io->context, MACCTL);
    if (macctl == 0xFFFFFFFFu || (macctl & MAC_BIG_ENDIAN) ||
        (macctl & (SHM_ENABLED | IHR_ENABLED)) != (SHM_ENABLED | IHR_ENABLED))
        return B43_BOOT_ACCESS;
    if (macctl & (PSM_RUN | MAC_ENABLED)) return B43_BOOT_BUSY;
    io->write32(io->context, MACCTL, macctl | PSM_JMP0);
    for (uint16_t i = 0; i < 64; i++) shm_write16(io, SHM_SCRATCH, i, 0);
    for (uint16_t i = 0; i < 4096; i += 2) shm_write16(io, SHM_SHARED, i, 0);
    control(io, SHM_UCODE | SHM_AUTOINC_W, 0);
    upload(io, &microcode);
    if (pcm) {
        control(io, SHM_HW, 0x01EA);
        io->write32(io->context, SHM_DATA, 0x00004000);
        control(io, SHM_HW, 0x01EB);
        upload(io, &pcmcode);
    }
    io->write32(io->context, IRQ_REASON, 0xFFFFFFFFu);
    io->write32(io->context, MACCTL, (macctl & ~PSM_JMP0) | PSM_RUN);
    int result = B43_BOOT_TIMEOUT;
    for (unsigned poll = 0; poll < 20; poll++) {
        uint32_t reason = io->read32(io->context, IRQ_REASON);
        if (reason == 0xFFFFFFFFu) { result = B43_BOOT_ACCESS; break; }
        /* As in Linux, extra IRQ bits do not constitute a successful boot. */
        if (reason == IRQ_SUSPENDED) {
            (void)io->read32(io->context, IRQ_REASON);
            b43_boot_info_t found = {0};
            found.revision = shared_read16(io, 0);
            found.patch = shared_read16(io, 2);
            found.date = shared_read16(io, 4);
            found.time = shared_read16(io, 6);
            if (found.revision == 0xFFFFu) { result = B43_BOOT_ACCESS; break; }
            if (found.revision <= 0x128u) { result = B43_BOOT_OLD_FIRMWARE; break; }
            found.header_format = found.revision >= 598 ? 598 : found.revision >= 410 ? 410 : 351;
            found.opensource = found.date == 0xFFFFu;
            if (found.opensource) found.patch = found.time;
            *info = found;
            return B43_BOOT_OK;
        }
        if (poll < 19) io->delay_us(io->context, 50000);
    }
    /* Preserve unrelated MAC control bits; never leave failed firmware running. */
    io->write32(io->context, MACCTL, (macctl & ~PSM_RUN) | PSM_JMP0);
    return result;
}

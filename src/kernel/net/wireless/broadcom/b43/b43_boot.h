/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NIXEL_B43_BOOT_H
#define NIXEL_B43_BOOT_H
#include "b43.h"

/* Register-level transport, separate from PCI/SSB window ownership. Callers
 * must serialize access and supply a powered, reset, clocked core with SHM/IHR
 * enabled, interrupts masked and DMA stopped. Firmware buffers must remain
 * immutable for the call. This interface does not configure or enable RF. */
typedef struct {
    void* context;
    uint16_t (*read16)(void*, uint16_t);
    uint32_t (*read32)(void*, uint16_t);
    void (*write16)(void*, uint16_t, uint16_t);
    void (*write32)(void*, uint16_t, uint32_t);
    void (*delay_us)(void*, uint32_t);
} b43_io_t;

typedef struct {
    uint16_t revision, patch, date, time;
    uint16_t header_format; /* 351, 410 or 598: future TX descriptor selection */
    uint8_t opensource;
} b43_boot_info_t;

enum {
    B43_BOOT_OK = 0, B43_BOOT_INVALID = -1, B43_BOOT_BUSY = -2,
    B43_BOOT_ACCESS = -3, B43_BOOT_TIMEOUT = -4, B43_BOOT_OLD_FIRMWARE = -5
};
/* Optional PCM: pass NULL/0 to omit it. Does not imply hwcrypto support.
 * Success leaves the PSM running with the MAC suspended. Boot failures after
 * upload stop the PSM at offset zero. Invalid input/busy cores are untouched.
 * Does not apply initvals; those follow boot in the chip initialization path. */
int b43_firmware_boot(const b43_io_t* io,
                      const uint8_t* ucode, uint32_t ucode_length,
                      const uint8_t* pcm, uint32_t pcm_length,
                      b43_boot_info_t* info);
#endif

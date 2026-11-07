/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NIXEL_IWLEGACY_INTERNAL_H
#define NIXEL_IWLEGACY_INTERNAL_H
#include "iwlegacy.h"
#include "csr.h"
#include "prph.h"
static inline uint16_t il_le16(const uint8_t* p) {
    return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}
static inline uint32_t il_le32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline int il_io_valid(const il_io_t* io) {
    return io && io->read32 && io->write32 && io->delay_us;
}
extern int iwlegacy_native_owned;
int il3945_apm_start(const il_io_t* io);
int il3945_apm_stop(const il_io_t* io);
/* PCI mastering disabled; no host DMA buffers have been posted. */
int il3945_reset_unowned(const il_io_t* io);
int il_poll(const il_io_t* io, uint32_t reg, uint32_t mask,
             uint32_t bits, unsigned timeout_us);
int il_update(const il_io_t* io, uint32_t reg, uint32_t clear, uint32_t set);
int il_grab(const il_io_t* io);
int il_ack_interrupts(const il_io_t* io, uint32_t irq);
int il3945_read_error_log(const il_io_t*, uint32_t base, il3945_error_log_t*);
void il_release(const il_io_t* io);
uint32_t il_prph_read(const il_io_t* io, uint32_t reg);
void il_prph_write(const il_io_t* io, uint32_t reg, uint32_t value);
#endif

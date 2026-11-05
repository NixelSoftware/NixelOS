/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NIXEL_IWLEGACY_RX_H
#define NIXEL_IWLEGACY_RX_H
#include "iwlegacy.h"
#define IL_RX_COUNT 256u
#define IL_RX_BYTES 4096u
/* x86 coherent DMA memory. The caller owns and pins every allocation until
 * stop confirms idle. Callback payloads are borrowed only for the callback. */
typedef struct { uint8_t* data; uint32_t dma; } il_rx_buffer_t;
typedef void (*il_rx_handler_t)(void*, uint8_t command, uint8_t flags,
                               uint16_t sequence, const uint8_t* payload, unsigned length);
typedef struct {
    volatile uint32_t* descriptors;
    uint32_t descriptors_dma;
    volatile uint16_t* status; /* 12 bytes, closed_rb_num is the first halfword */
    uint32_t status_dma;
    il_rx_buffer_t slots[IL_RX_COUNT];
    unsigned read, write, published, packets, malformed;
    uint8_t active, polling;
} il_rx_queue_t;
/* Zero-initialize q before its first prepare. Exactly 254 buffers are posted,
 * leaving the hardware's two-slot gap. */
int il3945_rx_prepare(il_rx_queue_t* q, volatile uint32_t* descriptors, uint32_t descriptors_dma,
                       volatile uint16_t* status, uint32_t status_dma,
                       const il_rx_buffer_t buffers[IL_RX_COUNT - 2]);
int il3945_rx_start(const il_io_t* io, il_rx_queue_t* q);
/* Returns number dispatched, or a negative error. Bounded to one ring pass. */
int il3945_rx_poll(const il_io_t* io, il_rx_queue_t* q, il_rx_handler_t handler, void* context);
int il3945_rx_stop(const il_io_t* io, il_rx_queue_t* q);
#endif

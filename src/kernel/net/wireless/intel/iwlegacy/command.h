/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NIXEL_IWLEGACY_COMMAND_H
#define NIXEL_IWLEGACY_COMMAND_H
#include "rx.h"
typedef struct {
    volatile uint8_t* descriptors; /* queue 4: 256 TFDs of 64 bytes */
    uint8_t* buffer; uint32_t buffer_dma;
    unsigned next, response_length;
    uint16_t sequence;
    uint8_t command, pending, poisoned, response_id, response_flags;
    int result;
    uint32_t timeout_gp1, timeout_tx_config, timeout_tx_status;
    uint32_t timeout_gp, timeout_fh, timeout_tx_base, timeout_scd_mode, timeout_scd_fifos;
    unsigned timeout_rx_read, timeout_rx_closed;
    uint8_t response[256];
} il_command_queue_t;
/* Zero-initialize q before first use; reset it only after DMA is stopped. */
int il3945_command_prepare(il_command_queue_t* q, volatile void* descriptors,
                            uint8_t* buffer, uint32_t buffer_dma);
/* One in-flight command. Buffers and descriptor ring remain pinned until
 * completion or confirmed DMA stop. A timeout poisons the queue until reset. */
int il3945_command_send(const il_io_t* io, il_rx_queue_t* rx, il_command_queue_t* q,
                         uint8_t command, const void* payload, unsigned length,
                         unsigned timeout_ms, il_rx_handler_t events, void* context);
#endif

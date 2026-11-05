/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NIXEL_IWLEGACY_SESSION_H
#define NIXEL_IWLEGACY_SESSION_H
#include "rx.h"
typedef struct {
    uint32_t init_code, init_data, runtime_code, runtime_data;
} il3945_fw_dma_t;
typedef struct {
    uint8_t init_alive, runtime_alive, major, minor;
    uint32_t event_log, error_log;
} il3945_alive_t;
/* RX must be active, TX rings configured, NIC clocks on and coherent firmware
 * DMA images pinned. Remains caller-owned on every return; stop/reset DMA
 * before releasing any memory. Success means runtime ALIVE, not association. */
int il3945_firmware_run(const il_io_t* io, il_rx_queue_t* rx,
                         const uint8_t* firmware, uint32_t size,
                         const il3945_fw_dma_t* dma, il3945_alive_t* alive);
/* Five 256-entry, 64-byte TFD rings, plus a 32-byte shared pointer table.
 * Caller zeroes and pins the rings and shared table before this call. */
int il3945_tx_setup(const il_io_t* io, volatile uint32_t* shared, uint32_t shared_dma,
                     const uint32_t rings[5]);
#endif

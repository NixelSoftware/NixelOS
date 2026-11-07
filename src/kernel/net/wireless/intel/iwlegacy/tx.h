/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NIXEL_IL3945_TX_H
#define NIXEL_IL3945_TX_H
#include "rx.h"
typedef struct {
    volatile uint8_t* descriptors;
    uint8_t* buffer;
    uint32_t dma;
    unsigned next;
    uint16_t sequence;
    uint8_t pending, poisoned;
    int result;
    il3945_tx_snapshot_t submitted; /* retained independently of DMA storage */
} il_tx_queue_t;
/* Queue 0, one in-flight frame. DMA storage remains pinned until completion
 * or confirmed device stop. A timeout must poison rather than recycle it.
 * Successful submission holds MAC access; the owner releases it after a
 * matched completion or during failure/stop cleanup. */
int il3945_tx_prepare(il_tx_queue_t*, volatile void*, uint8_t*, uint32_t);
int il3945_tx_submit(const il_io_t*, il_tx_queue_t*, const uint8_t*, unsigned,
                       uint8_t station, uint8_t plcp);
void il3945_tx_event(il_tx_queue_t*, uint8_t, uint16_t, const uint8_t*, unsigned);
#endif

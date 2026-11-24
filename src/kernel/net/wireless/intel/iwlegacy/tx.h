/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NIXEL_IL3945_TX_H
#define NIXEL_IL3945_TX_H
#include "rx.h"
/* mac80211 AC_BE=2; iwlegacy maps it to the best-effort FIFO. The
 * non-WMM, single-inflight station uses BE for all frames. */
#define IL3945_TX_QUEUE 2u
#define IL3945_TX_SLOTS 256u
#define IL3945_TX_SLOT_BYTES 4096u
#define IL3945_TX_STORAGE_BYTES (IL3945_TX_SLOTS * IL3945_TX_SLOT_BYTES)
typedef struct {
    volatile uint8_t* descriptors;
    uint8_t* buffer;
    uint32_t dma;
    unsigned next;
    uint8_t cts_protection;
    uint16_t sequence;
    uint8_t pending, poisoned;
    int result;
    il3945_tx_snapshot_t submitted; /* retained independently of DMA storage */
} il_tx_queue_t;
/* Best-effort queue, one in-flight frame, with distinct DMA storage for every TFD.
 * Supply IL3945_TX_STORAGE_BYTES of contiguous storage, retained for the
 * entire ring lifetime. A slot is reused only when the ring wraps after
 * matching completions; other descriptors never alias its command/payload.
 * The allocation stays pinned until confirmed device stop. A timeout must
 * poison the queue rather than recycle its storage.
 * Successful submission holds MAC access; the owner releases it after a
 * matched completion or during failure/stop cleanup. */
int il3945_tx_prepare(il_tx_queue_t*, volatile void*, uint8_t*, uint32_t, unsigned storage_bytes);
int il3945_tx_submit(const il_io_t*, il_tx_queue_t*, const uint8_t*, unsigned,
                       uint8_t station, uint8_t plcp);
void il3945_tx_event(il_tx_queue_t*, uint8_t, uint16_t, const uint8_t*, unsigned);
#endif

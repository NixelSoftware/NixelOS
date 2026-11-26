/* SPDX-License-Identifier: MIT */
#ifndef NIXEL_GEN4_ENGINE_H
#define NIXEL_GEN4_ENGINE_H
#include "i915.h"
/* Gen4 render command streamer: ring + status page + one batch buffer, all in a
 * single GGTT-mapped allocation. Strictly serialized (one batch in flight); the
 * caller holds the video command mutex. Page 0 ring, 1 status, 2-3 batch. */
#define GEN4_ENGINE_PAGES 4u
#define GEN4_BATCH_DWORDS ((2u * 4096u) / 4u - 2u)
typedef struct {
    i915_display_t* d;
    uint32_t gtt, physical, saved[GEN4_ENGINE_PAGES];
    volatile uint32_t* ring; volatile uint32_t* hws; uint32_t* batch;
    uint32_t tail, seqno;
    int ready, fault;
} gen4_engine_t;
/* Brings up the ring only when the engine is idle and firmware left it off. */
int gen4_engine_init(gen4_engine_t* e, i915_display_t* d);
/* Copies cmds into the batch buffer, runs it, returns its fence (0 on failure). */
uint32_t gen4_engine_submit(gen4_engine_t* e, const uint32_t* cmds, uint32_t count);
int gen4_engine_wait(gen4_engine_t* e, uint32_t fence, uint32_t milliseconds);
/* Ring + batch dispatch + fence round trip. */
int gen4_engine_selftest(gen4_engine_t* e);
/* Read-only: logs engine/GTT/error registers through io.trace. */
void gen4_engine_report(i915_display_t* d);
void gen4_engine_shutdown(gen4_engine_t* e);
#endif

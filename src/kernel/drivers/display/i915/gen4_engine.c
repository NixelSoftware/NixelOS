// SPDX-License-Identifier: MIT
/* Nixel gen4 command streamer bring-up. Programming sequence follows i915
 * intel_ringbuffer.c init_ring_common() and i965_dispatch_execbuffer(). */
#include "gen4_engine.h"
#include "i915_regs.h"

static uint32_t rd(i915_display_t* d, uint32_t r) { return d->io.read(d->io.context, r); }
static void wr(i915_display_t* d, uint32_t r, uint32_t v) {
    d->io.write(d->io.context, r, v); (void)rd(d, r);
}
static void T(i915_display_t* d, const char* text) {
    if (d->io.trace) d->io.trace(d->io.context, text, 0, 0);
}
static void TV(i915_display_t* d, const char* text, uint32_t value) {
    if (d->io.trace) d->io.trace(d->io.context, text, value, 1);
}
void gen4_engine_report(i915_display_t* d) {
    if (!d) return;
    TV(d, "RING_CTL ", rd(d, I915_RING_CTL)); TV(d, "RING_HEAD ", rd(d, I915_RING_HEAD));
    TV(d, "RING_TAIL ", rd(d, I915_RING_TAIL)); TV(d, "RING_START ", rd(d, I915_RING_START));
    TV(d, "HWS_PGA ", rd(d, I915_HWS_PGA)); TV(d, "ACTHD ", rd(d, I915_ACTHD));
    TV(d, "IPEIR ", rd(d, I915_IPEIR)); TV(d, "IPEHR ", rd(d, I915_IPEHR));
    TV(d, "EIR ", rd(d, I915_EIR)); TV(d, "ESR ", rd(d, I915_ESR));
    TV(d, "MI_MODE ", rd(d, I915_MI_MODE)); TV(d, "INSTPM ", rd(d, I915_INSTPM));
    TV(d, "PGTBL_CTL ", rd(d, I915_PGTBL_CTL)); TV(d, "PGTBL_ER ", rd(d, I915_PGTBL_ER));
}
static void* cpu(uint32_t physical) { return (void*)(unsigned long)physical; }

static int wait_reg(i915_display_t* d, uint32_t reg, uint32_t mask, uint32_t value,
                    uint32_t ms) {
    for (uint32_t n = 0; n < ms * 10u; n++) {
        if ((rd(d, reg) & mask) == value) return 1;
        d->io.delay_us(d->io.context, 100);
    }
    return (rd(d, reg) & mask) == value;
}
static int idle(gen4_engine_t* e, uint32_t ms) {
    for (uint32_t n = 0; n < ms * 10u; n++) {
        if ((rd(e->d, I915_RING_HEAD) & I915_RING_ADDR_MASK) == e->tail) return 1;
        e->d->io.delay_us(e->d->io.context, 100);
    }
    return (rd(e->d, I915_RING_HEAD) & I915_RING_ADDR_MASK) == e->tail;
}
int gen4_engine_init(gen4_engine_t* e, i915_display_t* d) {
    if (!e || !d || !d->attached || d->fault || !d->info) return 0;
    *e = (gen4_engine_t){0};
    e->d = d;
    T(d, "step: check engine is idle and off");
    /* Never take over an engine firmware left running. */
    if ((rd(d, I915_RING_CTL) & I915_RING_VALID) ||
        (rd(d, I915_RING_HEAD) & I915_RING_ADDR_MASK) !=
        (rd(d, I915_RING_TAIL) & I915_RING_ADDR_MASK)) {
        T(d, "FAIL: firmware left the ring running; not touching it");
        gen4_engine_report(d);
        return 0;
    }
    T(d, "step: reserve GGTT range and RAM");
    if (!i915_gtt_reserve(d, GEN4_ENGINE_PAGES, &e->gtt, &e->physical, e->saved)) {
        T(d, "FAIL: no free GGTT range or no contiguous RAM");
        return 0;
    }
    TV(d, "  gtt offset ", e->gtt); TV(d, "  physical ", e->physical);
    e->ring = cpu(e->physical);
    e->hws = cpu(e->physical + 4096u);
    e->batch = cpu(e->physical + 2u * 4096u);
    T(d, "step: stop ring (MI_MODE STOP_RING)");
    wr(d, I915_MI_MODE, I915_MASKED_ENABLE(I915_STOP_RING));
    if (!wait_reg(d, I915_MI_MODE, I915_MODE_IDLE, I915_MODE_IDLE, 100))
        TV(d, "warning: engine did not report idle, MI_MODE ", rd(d, I915_MI_MODE));
    T(d, "step: program ring registers");
    wr(d, I915_RING_CTL, 0);
    wr(d, I915_RING_HEAD, 0);
    wr(d, I915_RING_TAIL, 0);
    wr(d, I915_RING_START, e->gtt);
    wr(d, I915_HWS_PGA, e->gtt + 4096u);
    wr(d, I915_RING_CTL, ((4096u - 4096u) & I915_RING_SIZE_MASK) | I915_RING_VALID);
    if (!wait_reg(d, I915_RING_CTL, I915_RING_VALID, I915_RING_VALID, 50) ||
        rd(d, I915_RING_START) != e->gtt ||
        (rd(d, I915_RING_HEAD) & I915_RING_ADDR_MASK) != 0) {
        /* Ring never latched; the engine cannot have fetched anything. */
        T(d, "FAIL: ring did not latch");
        gen4_engine_report(d);
        wr(d, I915_RING_CTL, 0); wr(d, I915_HWS_PGA, 0);
        i915_gtt_unreserve(d, e->gtt, e->physical, GEN4_ENGINE_PAGES, e->saved);
        return 0;
    }
    T(d, "step: ring latched, start it (clear STOP_RING)");
    wr(d, I915_MI_MODE, I915_MASKED_DISABLE(I915_STOP_RING));
    e->ready = 1;
    return 1;
}
static uint32_t hws_read(gen4_engine_t* e, uint32_t offset) {
    e->d->io.flush_line(e->d->io.context, (const void*)(e->hws + offset / 4u));
    return e->hws[offset / 4u];
}
int gen4_engine_wait(gen4_engine_t* e, uint32_t fence, uint32_t milliseconds) {
    if (!e || !e->ready || e->fault) return 0;
    for (uint32_t n = 0; n < milliseconds * 10u; n++) {
        if ((int32_t)(hws_read(e, I915_HWS_SEQNO) - fence) >= 0) return 1;
        e->d->io.delay_us(e->d->io.context, 100);
    }
    e->fault = 1; /* GPU hang: leave memory mapped, it may still be fetching */
    TV(e->d, "FAIL: fence timeout, wanted seqno ", fence);
    TV(e->d, "  hws seqno ", e->hws[I915_HWS_SEQNO / 4u]);
    gen4_engine_report(e->d);
    /* Don't leave a wedged ring fetching: stop it (Linux does the same on hang).
     * RAM and GGTT mappings stay in place. */
    wr(e->d, I915_MI_MODE, I915_MASKED_ENABLE(I915_STOP_RING));
    wr(e->d, I915_RING_CTL, 0);
    return 0;
}
uint32_t gen4_engine_submit(gen4_engine_t* e, const uint32_t* cmds, uint32_t count) {
    if (!e || !e->ready || e->fault || !cmds || !count || count > GEN4_BATCH_DWORDS)
        return 0;
    /* One batch in flight and an empty ring: no space accounting or wrap races. */
    if (e->seqno && !gen4_engine_wait(e, e->seqno, 1000)) return 0;
    if (!idle(e, 100)) {
        e->fault = 1; T(e->d, "FAIL: ring not idle before submit"); gen4_engine_report(e->d);
        return 0;
    }
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; i++) e->batch[n++] = cmds[i];
    e->batch[n++] = MI_BATCH_BUFFER_END;
    if (n & 1u) e->batch[n++] = MI_NOOP; /* qword-aligned */
    uint32_t seq = e->seqno + 1u;
    /* Commands are 32 bytes from a 32-byte boundary, so groups never straddle
     * the end of the 4 KiB ring; the tail simply wraps to 0 and head follows. */
    uint32_t c = e->tail / 4u;
    e->ring[c++] = MI_FLUSH | MI_EXE_FLUSH;
    e->ring[c++] = MI_BATCH_BUFFER_START | MI_BATCH_GTT;
    e->ring[c++] = e->gtt + 2u * 4096u;
    e->ring[c++] = MI_FLUSH | MI_EXE_FLUSH;
    e->ring[c++] = MI_STORE_DWORD_INDEX;
    e->ring[c++] = I915_HWS_SEQNO;
    e->ring[c++] = seq;
    e->ring[c++] = MI_NOOP; /* 8 dwords from a 32-byte boundary: qword aligned */
    e->d->io.flush_cpu(e->d->io.context); /* GPU reads RAM through uncached GTT */
    e->seqno = seq;
    e->tail = (c * 4u) & 4095u;
    TV(e->d, "step: submit batch, tail ", e->tail);
    wr(e->d, I915_RING_TAIL, e->tail);
    return seq;
}
int gen4_engine_selftest(gen4_engine_t* e) {
    if (!e || !e->ready) return 0;
    const uint32_t magic = 0x4E495845u; /* 'NIXE' */
    uint32_t batch[] = { MI_STORE_DWORD_INDEX, I915_HWS_SCRATCH, magic, MI_NOOP };
    e->hws[I915_HWS_SCRATCH / 4u] = 0;
    uint32_t fence = gen4_engine_submit(e, batch, 4);
    if (!fence) { T(e->d, "FAIL: submit refused"); return 0; }
    if (!gen4_engine_wait(e, fence, 100)) return 0;
    uint32_t got = hws_read(e, I915_HWS_SCRATCH);
    if (got != magic) { TV(e->d, "FAIL: batch result wrong, read ", got); return 0; }
    T(e->d, "step: batch executed and fence signalled");
    return 1;
}
void gen4_engine_shutdown(gen4_engine_t* e) {
    if (!e || !e->ready) return;
    e->ready = 0;
    T(e->d, "step: shutdown");
    if (!e->fault && idle(e, 100)) {
        wr(e->d, I915_MI_MODE, I915_MASKED_ENABLE(I915_STOP_RING));
        wr(e->d, I915_RING_CTL, 0);
        wr(e->d, I915_HWS_PGA, 0);
        i915_gtt_unreserve(e->d, e->gtt, e->physical, GEN4_ENGINE_PAGES, e->saved);
    } /* else the engine may still fetch: keep the mapping and RAM */
}

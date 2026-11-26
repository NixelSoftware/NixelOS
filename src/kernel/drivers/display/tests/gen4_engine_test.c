/* Host test: a software model of the gen4 render command streamer consumes the
 * driver's ring, so we verify register sequencing, GGTT mapping, MI encodings,
 * fencing, wraparound and hang handling. It does NOT prove real silicon accepts
 * them; that needs a GM965/GM45 machine. */
#define _GNU_SOURCE
#include <stdint.h>
/* Kernel types.h clashes with libc; supply what the headers need instead. */
#define TYPES_H
#define SCREEN_MAX_WIDTH 1920
#define SCREEN_MAX_HEIGHT 1080
#define SCREEN_DEFAULT_WIDTH 800
#define SCREEN_DEFAULT_HEIGHT 600
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "../i915/gen4_engine.h"
#include "../i915/i915_regs.h"

static uint32_t regs[0x100000 / 4];
static int hang, executed, ring_enabled;
static uint32_t next_page = 0x10000000u; /* low addresses model i386 phys */

static uint8_t* gtt_to_host(uint32_t gtt) {
    uint32_t pte = regs[(I915_GTT + (gtt / 4096u) * 4u) / 4u];
    if (!(pte & 1u)) { fprintf(stderr, "GPU fetch via invalid PTE %x\n", gtt); exit(1); }
    return (uint8_t*)(uintptr_t)((pte & ~4095u) + (gtt & 4095u));
}
static void run_ring(void) {
    uint32_t start = regs[I915_RING_START / 4], tail = regs[I915_RING_TAIL / 4];
    uint32_t head = regs[I915_RING_HEAD / 4] & I915_RING_ADDR_MASK;
    while (head != tail) {
        uint32_t* p = (uint32_t*)gtt_to_host(start + head);
        uint32_t op = *p;
        if (op == MI_NOOP) { head = (head + 4) & 4095u; continue; }
        if ((op >> 23) == 4) { head = (head + 4) & 4095u; continue; }
        if ((op >> 23) == 0x31 && (op & MI_BATCH_GTT)) {
            uint32_t* b = (uint32_t*)gtt_to_host(p[1]);
            for (;; b++) {
                uint32_t bo = *b;
                if (bo == MI_BATCH_BUFFER_END) break;
                if (bo == MI_NOOP) continue;
                if (bo == MI_STORE_DWORD_INDEX) {
                    uint32_t* hws = (uint32_t*)gtt_to_host(regs[I915_HWS_PGA / 4]);
                    if (b[1] < 0x80) { fprintf(stderr, "store into hardware-reserved HWS area\n"); exit(1); }
                    hws[b[1] / 4] = b[2]; b += 2; continue;
                }
                fprintf(stderr, "bad batch op %08x\n", bo); exit(1);
            }
            head = (head + 8) & 4095u; continue;
        }
        if (op == MI_STORE_DWORD_INDEX) {
            uint32_t* hws = (uint32_t*)gtt_to_host(regs[I915_HWS_PGA / 4]);
            if (p[1] < 0x80) { fprintf(stderr, "store into hardware-reserved HWS area\n"); exit(1); }
            hws[p[1] / 4] = p[2]; head = (head + 12) & 4095u; continue;
        }
        fprintf(stderr, "bad ring op %08x at %u\n", op, head); exit(1);
    }
    regs[I915_RING_HEAD / 4] = head; executed++;
}
static uint32_t rd(void* c, uint32_t r) {
    (void)c;
    if (r == I915_MI_MODE) return regs[r / 4] | ((regs[r / 4] & I915_STOP_RING) ? I915_MODE_IDLE : 0);
    return regs[r / 4];
}
static void wr(void* c, uint32_t r, uint32_t v) {
    (void)c;
    if (r == I915_RING_HEAD) { regs[r / 4] = v; return; }
    if (r == I915_MI_MODE) { /* masked-bit register */
        uint32_t m = v >> 16; regs[r / 4] = (regs[r / 4] & ~m) | (v & m & 0xFFFFu); return;
    }
    regs[r / 4] = v;
    if (r == I915_RING_CTL) ring_enabled = v & 1;
    if (r == I915_RING_TAIL && ring_enabled && !hang && !(regs[I915_MI_MODE / 4] & I915_STOP_RING)) run_ring();
}
static void delay(void* c, uint32_t us) { (void)c; (void)us; }
static uint32_t alloc(void* c, uint32_t pages) {
    (void)c;
    void* m = mmap((void*)(uintptr_t)next_page, pages * 4096u, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (m == MAP_FAILED) return 0;
    next_page += pages * 4096u;
    return (uint32_t)(uintptr_t)m;
}
static void release(void* c, uint32_t p, uint32_t n) { (void)c; munmap((void*)(uintptr_t)p, n * 4096u); }
static int steps, fails;
static char last_fail[96];
static void trace(void* c, const char* t, uint32_t v, int hv) {
    (void)c; (void)v; (void)hv;
    if (!strncmp(t, "step", 4)) steps++;
    if (!strncmp(t, "FAIL", 4)) { fails++; strncpy(last_fail, t, sizeof last_fail - 1); }
}
static void nop1(void* c) { (void)c; }
static void nop2(void* c, const void* a) { (void)c; (void)a; }
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static i915_display_t* make(i915_display_t* d) {
    i915_io_t io = {0, rd, wr, delay, alloc, release, nop1, nop2, trace};
    steps = fails = 0; last_fail[0] = 0; memset(regs, 0, sizeof regs); regs[I915_MI_MODE / 4] = I915_STOP_RING; /* firmware left it stopped */ hang = executed = ring_enabled = 0;
    i915_display_adopt(d, &io, i915_gen4_lookup(0x2A02), 0, 0, 0);
    return d;
}
int main(void) {
    CHECK(i915_gen4_lookup(0x2A02) && !i915_gen4_lookup(0x2A02)->g4x);
    CHECK(i915_gen4_lookup(0x2E22)->g4x && !i915_gen4_lookup(0x1234));
    static i915_display_t d; static gen4_engine_t e;
    make(&d);
    CHECK(gen4_engine_init(&e, &d));
    CHECK(regs[I915_RING_CTL / 4] == I915_RING_VALID);
    CHECK(regs[I915_HWS_PGA / 4] == e.gtt + 4096u);
    CHECK(!(regs[I915_MI_MODE / 4] & I915_STOP_RING));
    CHECK(gen4_engine_selftest(&e));
    CHECK(steps >= 5 && !fails);
    for (int i = 0; i < 300; i++) { /* wraps the 4 KiB ring more than once */
        uint32_t cmd[] = { MI_STORE_DWORD_INDEX, I915_HWS_SCRATCH, 0xC0DE0000u + i };
        uint32_t f = gen4_engine_submit(&e, cmd, 3);
        CHECK(f && gen4_engine_wait(&e, f, 10));
        CHECK(e.hws[I915_HWS_SCRATCH / 4] == 0xC0DE0000u + i);
    }
    CHECK(!gen4_engine_submit(&e, (uint32_t[]){0}, GEN4_BATCH_DWORDS + 1));
    uint32_t saved_phys = e.physical, saved_gtt = e.gtt;
    gen4_engine_shutdown(&e);
    CHECK(regs[I915_MI_MODE / 4] & I915_STOP_RING);
    CHECK(regs[I915_RING_CTL / 4] == 0 && regs[I915_HWS_PGA / 4] == 0);
    CHECK(!(regs[(I915_GTT + (saved_gtt / 4096u) * 4u) / 4u] & 1u));
    (void)saved_phys;
    /* Firmware left the ring running: must not be touched. */
    make(&d); regs[I915_RING_CTL / 4] = I915_RING_VALID;
    CHECK(!gen4_engine_init(&e, &d));
    CHECK(regs[I915_RING_START / 4] == 0);
    CHECK(fails == 1 && strstr(last_fail, "firmware"));
    /* Hung GPU: selftest fails, engine faults, memory stays mapped. */
    make(&d); CHECK(gen4_engine_init(&e, &d)); hang = 1;
    CHECK(!gen4_engine_selftest(&e) && e.fault);
    CHECK(fails >= 1 && strstr(last_fail, "fence timeout"));
    gen4_engine_shutdown(&e);
    CHECK(regs[(I915_GTT + (e.gtt / 4096u) * 4u) / 4u] & 1u);
    puts("PASS: gen4 ring bring-up, batch dispatch, fences, wraparound, "
         "firmware-owned and hung engine handling");
    return 0;
}

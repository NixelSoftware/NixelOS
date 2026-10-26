#include "../mutex.h"
/* Sound Blaster 16: base 0x220, IRQ 5, 16-bit DMA channel 5.
 * Original ECE391 driver by Soumithri Bala; ported to NixelOS.
 */
#include "sb16_driver.h"
#include "serial.h"

extern volatile uint32_t frame_timer;
static uint32_t sample_rate, block_length, block_started;
static uint32_t accepted_bytes, completed_bytes, completed_blocks;
static uint32_t slow_blocks, refill_stalls;

static void report(const char* name, uint32_t value) {
    debug_print(name); debug_hex(value);
}


#define AUDIO_SLOTS 8u

/* Identity-mapped kernel memory, below 16 MiB, within one ISA DMA page. */
static uint8_t dma_buffer[SB16_BUFFER_SIZE * AUDIO_SLOTS] __attribute__((aligned(131072)));
static volatile int playing;
static int opened;
#define SLOT_FILLING 0xFFFFFFFFu
static volatile uint32_t slot_length[AUDIO_SLOTS];
static volatile uint32_t read_slot, write_slot, queued_count;
static uint32_t active_slot, auto_start_slot, first_queued;
static int auto_dma, auto_exit_pending, draining;

#define irq_save task_irq_save
#define irq_restore task_irq_restore

static uint8_t saved_irq_mask;

static int dsp_write(uint8_t value) {
    for (uint32_t i = 0; i < 1024u; i++) {
        if (!(inb(0x22C) & 0x80)) { outb(0x22C, value); return 0; }
    }
    return -1;
}

static int reset(void) {
    outb(0x226, 1);
    for (volatile uint32_t i = 0; i < 10000u; i++) { }
    outb(0x226, 0);
    for (uint32_t i = 0; i < 100000u; i++)
        if ((inb(0x22E) & 0x80) && inb(0x22A) == 0xAA) return 0;
    return -1;
}

/* Safe in IRQ context: no reset delays or serial output. */
static void quiesce(void) {
    outb(0xD4, 5);
    outb(0x21, inb(0x21) | 0x20u);
    playing = -1;
    queued_count = 0;
}

void sb16_shutdown(void) {
    MUTEX_GUARD(audio_mutex);
    if (!opened) return;
    uint32_t flags = irq_save();
    quiesce();
    opened = 0;
    irq_restore(flags);
    /* Reset and diagnostics can be slow; keep timer/input IRQs available. */
    (void)reset();
    flags = irq_save();
    outb(0x21, (inb(0x21) & ~0x20u) | saved_irq_mask);
    irq_restore(flags);
    debug_print("[AUDIO] playback counters (hex):");
    report(" rate=", sample_rate);
    report(" accepted=", accepted_bytes); report(" completed=", completed_bytes);
    report(" blocks=", completed_blocks); report(" slow_blocks=", slow_blocks);
    report(" refill_stalls=", refill_stalls); debug_print("\n");
}

int32_t sb16_init(uint32_t rate) {
    MUTEX_GUARD(audio_mutex);
    if (opened || rate < 5000u || rate > 48000u ||
        (uint32_t)dma_buffer + sizeof(dma_buffer) > 0x1000000u) return -1;
    if (reset() < 0) return -1;
    /* Require an SB16 DSP (major version >= 4). */
    if (dsp_write(0xE1) < 0) return -1;
    uint8_t version[2];
    for (uint32_t n = 0; n < 2; n++) {
        uint32_t i;
        for (i = 0; i < 100000u && !(inb(0x22E) & 0x80); i++) { }
        if (i == 100000u) return -1;
        version[n] = inb(0x22A);
    }
    if (version[0] < 4) return -1;
    outb(0x224, 0x80); outb(0x225, 2); /* IRQ 5 */
    outb(0x224, 0x81); outb(0x225, 0x22); /* DMA 1 and 5 */
    saved_irq_mask = inb(0x21) & 0x20;
    sample_rate = rate;
    accepted_bytes = completed_bytes = completed_blocks = 0;
    slow_blocks = refill_stalls = 0;
    active_slot = read_slot = write_slot = queued_count = 0;
    auto_dma = draining = 0;
    for (uint32_t i = 0; i < AUDIO_SLOTS; i++) slot_length[i] = 0;
    playing = 0;
    opened = 1;
    if (dsp_write(0x41) < 0 || dsp_write(rate >> 8) < 0 ||
        dsp_write(rate) < 0 || dsp_write(0xD1) < 0) {
        sb16_shutdown(); return -1;
    }
    (void)inb(0x22F);
    outb(0x21, inb(0x21) & ~0x20u);
    return 0;
}

/* Ask the DSP to stop exactly at the declared end of the stream or before
 * a known short tail. Otherwise an auto-init device may fetch into the next slot
 * before the CPU handles the boundary IRQ. */
static int prepare_boundary(void) {
    if (auto_dma && !auto_exit_pending &&
        ((draining && queued_count == 1) ||
         (slot_length[(active_slot + 1u) % AUDIO_SLOTS] > 0 &&
          slot_length[(active_slot + 1u) % AUDIO_SLOTS] < SB16_BUFFER_SIZE))) {
        if (dsp_write(0xD9) < 0) { quiesce(); return -1; }
        auto_exit_pending = 1;
    }
    return 0;
}

/* Full blocks use a cyclic DMA buffer: the DSP and DMA controller continue
 * at block boundaries without waiting for the interrupt handler. Short writes
 * use an exact-length single-cycle transfer, preserving the public ABI. */
static int start_block(uint32_t slot) {
    uint32_t length = slot_length[slot];
    auto_dma = length == SB16_BUFFER_SIZE;
    auto_exit_pending = 0;
    auto_start_slot = slot;
    uint32_t address = (uint32_t)(dma_buffer + slot * SB16_BUFFER_SIZE);
    uint32_t dma_length = auto_dma ? (AUDIO_SLOTS - slot) * SB16_BUFFER_SIZE : length;
    uint32_t count = dma_length / 2u - 1u;
    outb(0xD4, 5);
    outb(0xD8, 0);
    outb(0xD6, auto_dma ? 0x59 : 0x49);
    outb(0xC4, address >> 1); outb(0xC4, address >> 9);
    outb(0x8B, address >> 16);
    outb(0xD8, 0);
    outb(0xC6, count); outb(0xC6, count >> 8);
    active_slot = slot;
    block_length = length;
    block_started = frame_timer;
    /* SB16 auto-init counts 16-bit samples, including both channels. QEMU's
     * single-cycle stereo command instead expands a frame count by 4 bytes. */
    uint32_t dsp_count = length / (auto_dma ? 2u : 4u) - 1u;
    playing = 1;
    outb(0xD4, 1);
    if (dsp_write(auto_dma ? 0xB6 : 0xB0) < 0 || dsp_write(0x30) < 0 ||
        dsp_write(dsp_count) < 0 || dsp_write(dsp_count >> 8) < 0) {
        quiesce(); return -1;
    }
    return prepare_boundary();
}

static void pause_dma(void) {
    outb(0xD4, 5);
    if (auto_dma && dsp_write(0xD5) < 0) { quiesce(); return; }
    playing = 0;
}

void sb16_service(void) {
    MUTEX_GUARD(audio_mutex);
    uint32_t flags = irq_save();
    if (opened && playing == 0 && queued_count &&
        slot_length[read_slot] != SLOT_FILLING) {
        /* Two blocks of prefill avoid starting a full-rate stream with only
         * one block ready. A short/final stream starts within 20 ms. */
        if (draining || queued_count >= 2 || slot_length[read_slot] < SB16_BUFFER_SIZE ||
            (uint32_t)(frame_timer - first_queued) >= 2u) {
            (void)start_block(read_slot);
        }
    }
    irq_restore(flags);
}

int32_t sb16_drain(void) {
    MUTEX_GUARD(audio_mutex);
    uint32_t flags = irq_save();
    if (!opened || playing < 0) { irq_restore(flags); return -1; }
    draining = 1;
    int result = prepare_boundary();
    irq_restore(flags);
    sb16_service();
    return result;
}

int32_t sb16_write(const void* data, uint32_t length) {
    MUTEX_GUARD(audio_mutex);
    if (!opened || playing < 0 || draining || !data || !length || length > SB16_BUFFER_SIZE || (length & 3u)) return -1;
    uint32_t flags = irq_save();
    if (queued_count == AUDIO_SLOTS) { irq_restore(flags); return 0; }
    uint32_t slot = write_slot;
    if (!queued_count) {
        first_queued = frame_timer;
        if (completed_blocks) refill_stalls++;
    }
    slot_length[slot] = SLOT_FILLING;
    write_slot = (slot + 1u) % AUDIO_SLOTS;
    queued_count++;
    irq_restore(flags);
    /* Only this producer owns the reserved slot. The IRQ cannot consume a
     * SLOT_FILLING entry; all sample copying leaves timer/input IRQs enabled. */
    const uint32_t* source = data;
    uint32_t* dest = (uint32_t*)(dma_buffer + slot * SB16_BUFFER_SIZE);
    uint32_t count = length / sizeof(uint32_t);
    __asm__ volatile ("cld; rep movsl" : "+S"(source), "+D"(dest), "+c"(count) : : "memory");
    flags = irq_save();
    if (playing < 0) { irq_restore(flags); return -1; }
    slot_length[slot] = length;
    accepted_bytes += length;
    irq_restore(flags);
    sb16_service();
    return (int32_t)length;
}

int32_t sb16_status(void) {
    MUTEX_GUARD(audio_mutex);
    sb16_service();
    return !opened || playing < 0 ? -1 : (playing || queued_count ? 1 : 0);
}

/* Normal full-block IRQs only acknowledge, account, and release a queue slot.
 * DMA runs continuously; no copies, disk I/O, mutexes or DSP restarts here.
 * Reprogram only on drain/underrun, short writes, or a nonzero-base restart. */
void sb16_interrupt(void) {
    (void)inb(0x22F);
    if (opened && playing > 0) {
        uint32_t elapsed = frame_timer - block_started;
        uint32_t expected = (block_length * 100u + sample_rate * 4u - 1u) / (sample_rate * 4u);
        if (elapsed > expected + expected / 4u + 2u) slow_blocks++;
        completed_bytes += block_length;
        completed_blocks++;
        slot_length[active_slot] = 0;
        read_slot = (active_slot + 1u) % AUDIO_SLOTS;
        queued_count--;
        uint32_t next_length = slot_length[read_slot];
        if (queued_count && next_length && next_length != SLOT_FILLING) {
            if (auto_dma && !auto_exit_pending && next_length == SB16_BUFFER_SIZE &&
                (read_slot != 0 || auto_start_slot == 0)) {
                active_slot = read_slot;
                block_length = next_length;
                block_started = frame_timer;
                (void)prepare_boundary();
            } else {
                pause_dma();
                if (playing >= 0) (void)start_block(read_slot);
            }
        } else {
            if (queued_count) refill_stalls++; /* producer still copying */
            pause_dma();
        }
    }
    outb(0x20, 0x20);
}

/* OPL2-compatible mode also works on an SB16's OPL3. Use ISA I/O reads
 * for the address/data recovery delays (6 and 35 reads respectively). */
void sb16_opl_write(uint8_t reg, uint8_t value) {
    uint32_t flags = irq_save();
    outb(0x388, reg);
    for (int i = 0; i < 6; i++) (void)inb(0x388);
    outb(0x389, value);
    for (int i = 0; i < 35; i++) (void)inb(0x388);
    irq_restore(flags);
}

int32_t sb16_opl_init(void) {
    MUTEX_GUARD(audio_mutex);
    if (opened) {
        debug_print("[SB16] OPL probe skipped: PCM device busy\n");
        return -1;
    }
    if (reset() < 0) {
        debug_print("[SB16] OPL unavailable: sound card not responding\n");
        return -1;
    }
    sb16_opl_write(4, 0x60); sb16_opl_write(4, 0x80);
    uint8_t before = inb(0x388);
    sb16_opl_write(2, 255); sb16_opl_write(4, 0x21);
    /* More than 80 us on an ISA bus; bounded even with no device. */
    for (int i = 0; i < 200; i++) (void)inb(0x388);
    uint8_t after = inb(0x388);
    sb16_opl_write(4, 0x60); sb16_opl_write(4, 0x80);
    if ((before & 0xE0) || (after & 0xE0) != 0xC0) {
        debug_print("[SB16] OPL unavailable: timer probe failed at port 0x388\n");
        return -1;
    }
    for (int i = 0x20; i < 256; i++) sb16_opl_write(i, 0);
    sb16_opl_write(1, 0x20); /* waveform select enabled */
    sb16_opl_write(8, 0);
    /* SB16 master L/R and FM L/R: five-bit volume in bits 7:3.
     * Use unity mixer levels; the synth controls volume via OPL TL.
     * QEMU's separate AdLib device bypasses this SB16 mixer. */
    outb(0x224, 0x30); outb(0x225, 0xF8);
    outb(0x224, 0x31); outb(0x225, 0xF8);
    outb(0x224, 0x34); outb(0x225, 0xF8);
    outb(0x224, 0x35); outb(0x225, 0xF8);
    debug_print("[SB16] OPL available at port 0x388 (OPL2-compatible mode)\n");
    return 0;
}

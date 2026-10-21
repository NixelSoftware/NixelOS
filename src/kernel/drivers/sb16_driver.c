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


/* Identity-mapped kernel memory, below 16 MiB, within one ISA DMA page. */
static uint8_t dma_buffer[SB16_BUFFER_SIZE * 2] __attribute__((aligned(131072)));
static volatile int playing;
static int opened;
static uint32_t active_slot, queued_length;

static uint32_t irq_save(void) {
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}
static void irq_restore(uint32_t flags) {
    __asm__ volatile ("pushl %0; popfl" : : "r"(flags) : "memory", "cc");
}

static uint8_t saved_irq_mask;

static int dsp_write(uint8_t value) {
    for (uint32_t i = 0; i < 100000u; i++) {
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

void sb16_shutdown(void) {
    if (!opened) return;
    uint32_t flags = irq_save();
    debug_print("[AUDIO] playback counters (hex):");
    report(" rate=", sample_rate);
    report(" accepted=", accepted_bytes); report(" completed=", completed_bytes);
    report(" blocks=", completed_blocks); report(" slow_blocks=", slow_blocks);
    report(" refill_stalls=", refill_stalls); debug_print("\n");
    outb(0xD4, 5); /* mask DMA channel 5 */
    (void)reset();
    outb(0x21, (inb(0x21) & ~0x20u) | saved_irq_mask);
    opened = playing = 0;
    queued_length = 0;
    irq_restore(flags);
}

int32_t sb16_init(uint32_t rate) {
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
    active_slot = queued_length = 0;
    opened = 1;
    if (dsp_write(0x41) < 0 || dsp_write(rate >> 8) < 0 ||
        dsp_write(rate) < 0 || dsp_write(0xD1) < 0) {
        sb16_shutdown(); return -1;
    }
    (void)inb(0x22F);
    outb(0x21, inb(0x21) & ~0x20u);
    return 0;
}

static int32_t start_block(uint32_t slot, uint32_t length) {
    uint32_t address = (uint32_t)(dma_buffer + slot * SB16_BUFFER_SIZE);
    uint32_t count = length / 2u - 1u;
    outb(0xD4, 5);
    outb(0xD8, 0);
    outb(0xD6, 0x49); /* single transfer, memory to device, channel 5 */
    outb(0xC4, address >> 1); outb(0xC4, address >> 9);
    outb(0x8B, address >> 16);
    outb(0xD8, 0);
    outb(0xC6, count); outb(0xC6, count >> 8);
    playing = 1;
    outb(0xD4, 1);
    /* DMA counts 16-bit words; single-cycle stereo DSP counts frames.
     * QEMU multiplies the latter by both channels and sample width.
     * Reusing the DMA count makes its DSP wait for twice the DMA bytes.
     */
    uint32_t dsp_count = length / 4u - 1u;
    block_length = length;
    block_started = frame_timer;
    if (dsp_write(0xB0) < 0 || dsp_write(0x30) < 0 ||
        dsp_write(dsp_count) < 0 || dsp_write(dsp_count >> 8) < 0) {
        sb16_shutdown(); return -1;
    }
    return (int32_t)length;
}

int32_t sb16_write(const void* data, uint32_t length) {
    if (!opened || !data || !length || length > SB16_BUFFER_SIZE || (length & 3u)) return -1;
    uint32_t flags = irq_save();
    if (queued_length) { irq_restore(flags); return 0; }
    uint32_t slot = playing ? active_slot ^ 1u : active_slot;
    const uint8_t* source = data;
    for (uint32_t i = 0; i < length; i++) dma_buffer[slot * SB16_BUFFER_SIZE + i] = source[i];
    int32_t result = length;
    if (playing) queued_length = length;
    else {
        if (completed_blocks) refill_stalls++;
        result = start_block(slot, length);
    }
    if (result > 0) accepted_bytes += length;
    irq_restore(flags);
    return result;
}

int32_t sb16_status(void) { return opened ? playing : -1; }

/* Start the queued block before returning to user space. No disk I/O here. */
void sb16_interrupt(void) {
    (void)inb(0x22F);
    if (opened && playing) {
        uint32_t elapsed = frame_timer - block_started;
        uint32_t expected = (block_length * 100u + sample_rate * 4u - 1u) / (sample_rate * 4u);
        if (elapsed > expected + expected / 4u + 2u) slow_blocks++;
        completed_bytes += block_length;
        completed_blocks++;
    }
    playing = 0;
    if (opened && queued_length) {
        uint32_t length = queued_length;
        queued_length = 0;
        active_slot ^= 1u;
        (void)start_block(active_slot, length);
    }
    outb(0x20, 0x20);
}

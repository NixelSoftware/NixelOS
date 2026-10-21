/* Sound Blaster 16: base 0x220, IRQ 5, 16-bit DMA channel 5.
 * Original ECE391 driver by Soumithri Bala; ported to NixelOS.
 */
#include "sb16_driver.h"

/* Identity-mapped kernel memory, below 16 MiB, within one ISA DMA page. */
static uint8_t dma_buffer[SB16_BUFFER_SIZE] __attribute__((aligned(131072)));
static volatile int playing;
static int opened;
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
    outb(0xD4, 5); /* mask DMA channel 5 */
    (void)reset();
    outb(0x21, (inb(0x21) & ~0x20u) | saved_irq_mask);
    opened = playing = 0;
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
    opened = 1;
    if (dsp_write(0x41) < 0 || dsp_write(rate >> 8) < 0 ||
        dsp_write(rate) < 0 || dsp_write(0xD1) < 0) {
        sb16_shutdown(); return -1;
    }
    (void)inb(0x22F);
    outb(0x21, inb(0x21) & ~0x20u);
    return 0;
}

int32_t sb16_write(const void* data, uint32_t length) {
    if (!opened || playing || !data || !length ||
        length > sizeof(dma_buffer) || (length & 3u)) return -1;
    const uint8_t* source = data;
    for (uint32_t i = 0; i < length; i++) dma_buffer[i] = source[i];
    uint32_t address = (uint32_t)dma_buffer;
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
    /* Single-cycle 16-bit signed stereo output; count is in words. */
    if (dsp_write(0xB0) < 0 || dsp_write(0x30) < 0 ||
        dsp_write(count) < 0 || dsp_write(count >> 8) < 0) {
        sb16_shutdown(); return -1;
    }
    return (int32_t)length;
}

int32_t sb16_status(void) { return opened ? playing : -1; }

/* The common ISR wrapper saves registers and returns with IRET. */
void sb16_interrupt(void) {
    (void)inb(0x22F);
    playing = 0;
    outb(0x20, 0x20);
}

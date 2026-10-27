#include "delay.h"
#include "../task.h"

static uint32_t cycles_per_us;
static uint8_t initialized;
static uint64_t tsc(void) {
    uint32_t low, high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}
static int has_tsc(void) {
    uint32_t original, changed;
    __asm__ volatile ("pushfl; popl %0" : "=r"(original));
    changed = original ^ (1u << 21);
    __asm__ volatile ("pushl %0; popfl; pushfl; popl %0" : "+r"(changed) :: "memory", "cc");
    __asm__ volatile ("pushl %0; popfl" :: "r"(original) : "memory", "cc");
    if (!((original ^ changed) & (1u << 21))) return 0;
    uint32_t a = 1, b, c, d;
    __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    return !!(d & 16);
}
/* Channel 2 one-shot, speaker muted. Only used at early PCI initialization
 * and on old CPUs without TSC. Never reprogram PIT channel 0 (scheduler). */
static int pit_wait(uint32_t us, uint64_t* elapsed) {
    uint32_t flags = task_irq_save();
    uint8_t gate = inb(0x61);
    outb(0x61, gate & ~3u);
    uint32_t ticks = (us * 1193u + 999u) / 1000u;
    if (!ticks) ticks = 1;
    outb(0x43, 0xB0);
    outb(0x42, ticks & 255); outb(0x42, ticks >> 8);
    uint64_t start = elapsed ? tsc() : 0;
    outb(0x61, (gate & ~2u) | 1);
    uint32_t budget;
    for (budget = 0; budget < 2000000; budget++) {
        if (inb(0x61) & 0x20) break;
        __asm__ volatile ("pause");
    }
    if (elapsed) *elapsed = tsc() - start;
    outb(0x61, gate);
    task_irq_restore(flags);
    return budget < 2000000;
}
void driver_delay_init(void) {
    if (initialized) return;
    initialized = 1;
    if (has_tsc()) {
        uint64_t elapsed;
        if (pit_wait(10000, &elapsed)) {
            /* Avoid a freestanding i386 dependency on __udivdi3. */
            uint64_t remainder = 0, quotient = 0;
            for (int bit = 63; bit >= 0; bit--) {
                remainder = (remainder << 1) | ((elapsed >> bit) & 1);
                if (remainder >= 10000) { remainder -= 10000; quotient |= 1ULL << bit; }
            }
            if (quotient >= 10 && quotient <= 100000) cycles_per_us = (uint32_t)quotient + (uint32_t)quotient / 100 + 1;
        }
    }
}
void driver_delay_us(uint32_t us) {
    if (!us) return;
    driver_delay_init();
    if (cycles_per_us) {
        uint64_t start = tsc(), duration = (uint64_t)cycles_per_us * us;
        while (tsc() - start < duration) __asm__ volatile ("pause");
    } else {
        while (us) {
            uint32_t part = us > 10000 ? 10000 : us;
            (void)pit_wait(part, 0);
            us -= part;
        }
    }
}

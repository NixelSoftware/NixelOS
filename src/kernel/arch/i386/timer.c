#include "../arch.h"

#define PIT_INPUT_HZ 1193182u
#define PIT_NS_PER_COUNT 838u
#define SCHED_TICK_HZ 1000u

static uint64_t pit_epoch, last_clock, tsc_origin;
static uint32_t tsc_cycles_per_us;
static uint32_t pit_divisor = PIT_INPUT_HZ / SCHED_TICK_HZ;
static int started;

static uint64_t divide(uint64_t value, uint32_t divisor) {
#if defined(__i386__)
    uint32_t high = (uint32_t)(value >> 32), low = (uint32_t)value;
    uint32_t qhigh = high / divisor, remainder = high % divisor, qlow;
    __asm__ volatile ("divl %2" : "=a"(qlow), "+d"(remainder)
        : "rm"(divisor), "0"(low) : "cc");
    return ((uint64_t)qhigh << 32) | qlow;
#else
    return value / divisor;
#endif
}

static uint64_t read_tsc(void) {
    uint32_t low, high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

static int has_tsc(void) {
    uint32_t original, changed;
    __asm__ volatile ("pushfl; popl %0" : "=r"(original));
    changed = original ^ (1u << 21);
    __asm__ volatile ("pushl %0; popfl; pushfl; popl %0" : "+r"(changed) : : "memory", "cc");
    __asm__ volatile ("pushl %0; popfl" : : "r"(original) : "memory", "cc");
    if (!((changed ^ original) & (1u << 21))) return 0;
    uint32_t leaf = 1, b, c, d;
    __asm__ volatile ("cpuid" : "+a"(leaf), "=b"(b), "=c"(c), "=d"(d));
    return (d & (1u << 4)) != 0;
}

static void calibrate_tsc(void) {
    if (!has_tsc()) return;
    /* Channel 2 gives a 10 ms reference without depending on delivered IRQs.
     * Speaker output stays disabled; restore the original gate afterwards. */
    uint8_t gate = inb(0x61);
    outb(0x61, gate & ~3u);
    outb(0x43, 0xB0);
    outb(0x42, 11932u & 255u); outb(0x42, 11932u >> 8);
    uint64_t begin = read_tsc();
    outb(0x61, (gate & ~2u) | 1u);
    uint32_t budget;
    for (budget = 0; budget < 1000000u; budget++) if (inb(0x61) & 0x20u) break;
    uint64_t elapsed = read_tsc() - begin;
    outb(0x61, gate);
    if (budget < 1000000u) {
        uint64_t rate = divide(elapsed, 10000u);
        if (rate >= 10 && rate <= 100000u) tsc_cycles_per_us = (uint32_t)rate;
    }
}

static void program_pit(uint32_t divisor) {
    outb(0x43, 0x34);
    outb(0x40, divisor & 255u);
    outb(0x40, divisor >> 8);
}

void arch_timer_init(uint32_t hz) {
    pit_divisor = hz ? PIT_INPUT_HZ / hz : PIT_INPUT_HZ / SCHED_TICK_HZ;
    program_pit(pit_divisor);
    outb(0x21, inb(0x21) & ~1u);
}

int arch_timer_started(void) {
    return started;
}

void arch_timer_start(uint32_t hz) {
    calibrate_tsc();
    pit_divisor = hz ? PIT_INPUT_HZ / hz : PIT_INPUT_HZ / SCHED_TICK_HZ;
    pit_epoch = last_clock = 0;
    tsc_origin = tsc_cycles_per_us ? read_tsc() : 0;
    program_pit(pit_divisor);
    outb(0x21, inb(0x21) & ~1u);
    started = 1;
}

uint64_t arch_timer_ticks(void) {
    /* TSC avoids losing execution time if an IRQ is delayed/coalesced.
     * A latched PIT phase is the no-TSC fallback. */
    if (!started) return last_clock;
    uint64_t now;
    if (tsc_cycles_per_us) {
        uint64_t counter = read_tsc();
        if (counter < tsc_origin) return last_clock;
        uint64_t cycles = counter - tsc_origin;
        uint64_t us = divide(cycles, tsc_cycles_per_us);
        uint32_t remainder = (uint32_t)(cycles - us * tsc_cycles_per_us);
        now = us * 1000u + (remainder * 1000u) / tsc_cycles_per_us;
    } else {
        outb(0x43, 0x00);
        uint32_t count = inb(0x40);
        count |= (uint32_t)inb(0x40) << 8;
        uint32_t phase = count <= pit_divisor ? (pit_divisor - count) * PIT_NS_PER_COUNT : 0;
        now = pit_epoch + phase;
    }
    if (now < last_clock) now = last_clock;
    last_clock = now;
    return now;
}

void arch_timer_irq_tick(void) {
    if (!started) return;
    pit_epoch += pit_divisor * PIT_NS_PER_COUNT;
}

void arch_timer_set_rate(uint32_t hz) {
    if (started) pit_epoch = arch_timer_ticks();
    pit_divisor = hz ? PIT_INPUT_HZ / hz : PIT_INPUT_HZ / SCHED_TICK_HZ;
    program_pit(pit_divisor);
}

uint32_t arch_timer_clock_source(void) {
    return tsc_cycles_per_us ? 1u : 2u;
}

uint32_t arch_timer_cycles_per_us(void) {
    return tsc_cycles_per_us;
}

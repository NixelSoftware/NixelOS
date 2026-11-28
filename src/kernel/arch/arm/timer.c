#include "types.h"
#include "timer.h"

#define DEFAULT_FREQUENCY 62500000u

static uint32_t frequency = DEFAULT_FREQUENCY;
static uint32_t reload;
static int started;
static uint64_t last_ns;

static inline uint32_t read_cntfrq(void) {
    uint32_t value;
    __asm__ volatile ("mrc p15, 0, %0, c14, c0, 0" : "=r"(value));
    return value;
}

static inline uint64_t read_cntpct(void) {
    uint32_t low, high;
    __asm__ volatile ("mrrc p15, 0, %0, %1, c14" : "=r"(low), "=r"(high));
    return ((uint64_t)high << 32) | low;
}

static inline void write_tval(uint32_t value) {
    __asm__ volatile ("mcr p15, 0, %0, c14, c2, 0" : : "r"(value) : "memory");
}

static inline void write_ctl(uint32_t value) {
    __asm__ volatile ("mcr p15, 0, %0, c14, c2, 1" : : "r"(value) : "memory");
    __asm__ volatile ("isb" : : : "memory");
}

uint32_t arm_timer_init(uint32_t hz) {
    uint32_t cpu = read_cntfrq();
    if (cpu) frequency = cpu;
    reload = frequency / hz;
    if (!reload) reload = 1;
    write_tval(reload);
    write_ctl(1);
    return frequency;
}

void arm_timer_rearm(void) {
    write_tval(reload);
    write_ctl(1);
}

uint32_t arm_timer_frequency(void) {
    return frequency;
}

void arch_timer_init(uint32_t hz) {
    arm_timer_init(hz);
}

void arch_timer_start(uint32_t hz) {
    arm_timer_init(hz);
    started = 1;
}

int arch_timer_started(void) {
    return started;
}

uint64_t arch_timer_ticks(void) {
    if (!started) return last_ns;
    uint64_t count = read_cntpct();
    uint64_t whole = count / frequency;
    uint64_t remainder = count - whole * frequency;
    uint64_t ns = whole * 1000000000ull + (remainder * 1000000000ull) / frequency;
    if (ns < last_ns) ns = last_ns;
    last_ns = ns;
    return ns;
}

void arch_timer_irq_tick(void) {
}

void arch_timer_set_rate(uint32_t hz) {
    arm_timer_init(hz);
}

uint32_t arch_timer_clock_source(void) {
    return 1;
}

uint32_t arch_timer_cycles_per_us(void) {
    return frequency / 1000000u;
}

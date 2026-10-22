/* Single-CPU EEVDF integration. Task/frame switching remains in task.c/ASM.
 * The fixed slot queue is bounded by MAX_TASKS; the same policy is exercised by
 * tools/eevdf-policy. All entry points require local IRQs disabled. */
#include "scheduler.h"
#include "task.h"
#include "sched_policy.h"

_Static_assert(MAX_TASKS == EE_TASKS, "policy/task slot count");
static struct {
    ee_queue policy;
    task_t *slots[MAX_TASKS], *current, *idle;
    uint64_t switches, idle_time_ns;
} rq;

/* Hardware clock arithmetic also needs an i386 implementation without libgcc. */
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
static int known(const task_t* t) {
    return t && t != rq.idle && t->id < MAX_TASKS && rq.slots[t->id] == t;
}
static uint32_t task_state(uint32_t state) {
    switch (state) {
    case EE_READY: return TASK_STATE_READY;
    case EE_RUNNING: return TASK_STATE_RUNNING;
    case EE_BLOCKED: return TASK_STATE_BLOCKED;
    case EE_SLEEPING: return TASK_STATE_SLEEPING;
    case EE_DEAD: return TASK_STATE_TERMINATED;
    default: return TASK_STATE_UNUSED;
    }
}
static void publish(void) {
    for (unsigned i = 0; i < MAX_TASKS; ++i) {
        task_t* t = rq.slots[i];
        if (!t) continue;
        const ee_entity* e = &rq.policy.task[i];
        uint32_t state = task_state(e->state);
        if (t->state != state) startup_trace_event(TR_TASK_STATE, t->id, state, 0);
        t->state = state;
        t->sched.weight = e->weight;
        t->sched.runnable = e->state == EE_READY || e->state == EE_RUNNING;
        t->sched.policy_member = e->member;
        t->sched.delayed = e->delayed;
        t->sched.queued = e->member && rq.policy.current != (int)i;
    }
    rq.idle->state = rq.policy.current < 0 ? TASK_STATE_RUNNING : TASK_STATE_READY;
}
static void apply(unsigned op, unsigned id, int arg, uint64_t value, uint64_t now) {
    uint64_t elapsed = now > rq.policy.now ? now - rq.policy.now : 0;
    if (rq.policy.current < 0 && elapsed > ~(uint64_t)0 - rq.idle_time_ns)
        __builtin_trap();
    if (ee_apply(&rq.policy, op, id, arg, value, now) != EE_OK) __builtin_trap();
    if (rq.current == rq.idle) rq.idle_time_ns += elapsed;
    publish();
}
uint32_t scheduler_weight(int nice) { return ee_weight(nice); }
void scheduler_init(task_t* boot, task_t* idle, uint64_t now) {
    rq = (__typeof__(rq)){0};
    ee_init(&rq.policy);
    rq.policy.now = now;
    rq.idle = idle;
    rq.current = idle;
    idle->sched = (sched_entity_t){0};
    rq.slots[boot->id] = boot;
    boot->sched = (sched_entity_t){0};
    apply(EE_CREATE, boot->id, 0, SCHED_INTERACTIVE_REQUEST_NS, now);
    apply(EE_PICK, 0, 0, 0, now);
    rq.current = boot;
}
int scheduler_can_reuse(const task_t* t) {
    if (!t || t == rq.current || t == rq.idle || t->id >= MAX_TASKS) return 0;
    const ee_entity* e = &rq.policy.task[t->id];
    return (e->state == EE_UNUSED || e->state == EE_DEAD) &&
        !e->member && !e->delayed && !e->relative_valid;
}
void scheduler_enqueue(task_t* t, int nice, uint64_t now) {
    scheduler_enqueue_request(t, nice, SCHED_REQUEST_NS, now);
}
void scheduler_enqueue_request(task_t* t, int nice, uint32_t request_ns, uint64_t now) {
    if (!scheduler_can_reuse(t) ||
        (t->state != TASK_STATE_UNUSED && t->state != TASK_STATE_TERMINATED)) return;
    if (rq.slots[t->id] && rq.slots[t->id] != t) __builtin_trap();
    rq.slots[t->id] = t;
    t->sched.nice = nice < -20 ? -20 : nice > 19 ? 19 : nice;
    apply(EE_CREATE, t->id, t->sched.nice, request_ns, now);
}
void scheduler_wake(task_t* t, uint64_t now) {
    if (!known(t) || (t->state != TASK_STATE_SLEEPING && t->state != TASK_STATE_BLOCKED)) return;
    apply(EE_WAKE, t->id, 0, 0, now);
}
void scheduler_block(task_t* t, uint64_t now) {
    if (!known(t) || t->state == TASK_STATE_UNUSED || t->state == TASK_STATE_TERMINATED) return;
    apply(EE_BLOCK, t->id, 0, 0, now);
}
void scheduler_sleep(task_t* t, uint64_t deadline, uint64_t now) {
    if (!known(t) || t->state == TASK_STATE_UNUSED || t->state == TASK_STATE_TERMINATED) return;
    /* Kernel API supplies an absolute deadline. Normalize the clock before
     * converting, including already-expired waits (which must not become 1ms). */
    uint64_t effective = now > rq.policy.now ? now : rq.policy.now;
    uint64_t duration = deadline > effective ? deadline - effective : 1;
    apply(EE_SLEEP, t->id, 0, duration, effective);
    if (deadline <= effective) apply(EE_WAKE, t->id, 0, 0, effective);
}
void scheduler_exit(task_t* t, uint64_t now) {
    if (!known(t) || t->state == TASK_STATE_UNUSED || t->state == TASK_STATE_TERMINATED) return;
    apply(EE_EXIT, t->id, 0, 0, now);
}
void scheduler_tick(uint64_t now) { apply(EE_TICK, 0, 0, 0, now); }
task_t* scheduler_pick_next(uint64_t now) {
    task_t* previous = rq.current;
    apply(EE_PICK, 0, 0, 0, now);
    rq.current = rq.policy.current < 0 ? rq.idle : rq.slots[rq.policy.current];
    if (!rq.current) __builtin_trap();
    if (rq.current != previous) rq.switches++;
    return rq.current;
}
int scheduler_need_reschedule(void) { return rq.policy.need_reschedule; }
uint64_t scheduler_current_runtime(uint64_t now) {
    if (!known(rq.current)) return 0;
    const ee_entity* e = &rq.policy.task[rq.current->id];
    uint64_t delta = e->state == EE_RUNNING && now > rq.policy.now ? now - rq.policy.now : 0;
    if (delta > ~(uint64_t)0 - e->runtime) __builtin_trap();
    return e->runtime + delta;
}
void scheduler_set_nice(task_t* t, int nice, uint64_t now) {
    if (!known(t) || t->state == TASK_STATE_UNUSED || t->state == TASK_STATE_TERMINATED) return;
    t->sched.nice = nice < -20 ? -20 : nice > 19 ? 19 : nice;
    apply(EE_NICE, t->id, t->sched.nice, 0, now);
}
void scheduler_get_info(scheduler_info_t* info) {
    if (!info) return;
    *info = (scheduler_info_t){.clock_ns = rq.policy.now, .switches = rq.switches,
        .idle_time_ns = rq.idle_time_ns};
    for (unsigned i = 0; i < MAX_TASKS; ++i) {
        const ee_entity* e = &rq.policy.task[i];
        if (e->state == EE_READY || e->state == EE_RUNNING) {
            info->runnable++; info->total_weight += e->weight;
        }
    }
}

#ifndef SCHEDULER_HOST_TEST
extern volatile uint32_t frame_timer;
static uint64_t pit_epoch, last_clock, tsc_origin, next_legacy_tick;
static uint32_t tsc_cycles_per_us;
static int started;
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
int scheduler_running(void) { return started; }
uint64_t scheduler_clock(void) {
    /* Call with IRQs disabled. TSC avoids losing execution time if an IRQ is
     * delayed/coalesced. A latched PIT phase is the no-TSC fallback. */
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
        uint32_t phase = count <= 1193u ? (1193u - count) * 838u : 0;
        now = pit_epoch + phase;
    }
    if (now < last_clock) now = last_clock;
    last_clock = now;
    return now;
}
void scheduler_irq_tick(void) {
    if (!started) { frame_timer++; return; }
    pit_epoch += 1000000u;
    uint64_t now = scheduler_clock();
    /* Keep the ABI clock at 100 Hz, accounting for delayed timer delivery. */
    if (now >= next_legacy_tick) {
        uint64_t ticks = divide(now - next_legacy_tick, 10000000u) + 1u;
        frame_timer += (uint32_t)ticks;
        next_legacy_tick += ticks * 10000000u;
    }
}
void scheduler_start(void) {
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    if (!started) {
        calibrate_tsc();
        pit_epoch = last_clock = 0; next_legacy_tick = 10000000u;
        tsc_origin = tsc_cycles_per_us ? read_tsc() : 0;
        outb(0x43, 0x34);
        outb(0x40, 1193u & 255u); outb(0x40, 1193u >> 8);
        outb(0x21, inb(0x21) & ~1u);
        started = 1;
        startup_trace_clock(tsc_cycles_per_us ? 1u : 2u, tsc_cycles_per_us);
    }
    __asm__ volatile ("pushl %0; popfl" : : "r"(flags) : "memory", "cc");
}
#endif

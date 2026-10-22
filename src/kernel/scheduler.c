/*

I may have spent 3 hours trying to get the scheduler operate smoothly. 
I am not saying it doesn't work, rather I am saying it works, but not as 
smooth as I would like it to be. When I wrote this code, only God and I knew 
how it worked, I took a 1-hour break, and now only God knows how it works.
This might cost you some of your sanity, and some part of your soul. 
If you are not ready to lose your sanity, do not read this code. 
If you are not ready to lose your soul, do not read this code.

If you still wish to proceed after all these warnings, I wish you good luck. 
You will need it.

Good luck, soldier.

- elyas-code
*/


#include "scheduler.h"
#include "task.h"
#include "startup_trace.h"

/* CFS nice weights; nice 0 is the virtual-runtime reference weight (1024).
 * Keep policy independent of hardware clock/stack switching for validation. */
static const uint32_t weights[40] = {
    88761,71755,56483,46273,36291,29154,23254,18705,14949,11916,
    9548,7620,6100,4904,3906,3121,2501,1991,1586,1277,
    1024,820,655,526,423,335,272,215,172,137,
    110,87,70,56,45,36,29,23,18,15
};
/* Account halted CPU time separately from runnable task execution. */
static uint64_t scheduler_idle_time_ns;

static struct {
    sched_entity_t nil, *root, *leftmost;
    task_t *current, *idle, *sleepers;
    uint64_t now, min_vruntime, sequence, switches;
    uint32_t nr_running, load;
    int need_reschedule;
} rq;
#define NIL (&rq.nil)

uint32_t scheduler_weight(int nice) {
    if (nice < -20) nice = -20;
    if (nice > 19) nice = 19;
    return weights[nice + 20];
}
/* Freestanding i386 has no libgcc 64-bit division runtime. */
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
static uint64_t virtual_delta(uint64_t ns, uint32_t weight) {
    return divide(ns * 1024u, weight);
}
static int before(sched_entity_t* a, sched_entity_t* b) {
    return a->vruntime < b->vruntime ||
        (a->vruntime == b->vruntime && a->sequence < b->sequence);
}
static sched_entity_t* minimum(sched_entity_t* n) {
    if (n == NIL) return NIL;
    while (n->left != NIL) n = n->left;
    return n;
}
static void rotate_left(sched_entity_t* x) {
    sched_entity_t* y = x->right;
    x->right = y->left;
    if (y->left != NIL) y->left->parent = x;
    y->parent = x->parent;
    if (x->parent == NIL) rq.root = y;
    else if (x == x->parent->left) x->parent->left = y;
    else x->parent->right = y;
    y->left = x; x->parent = y;
}
static void rotate_right(sched_entity_t* x) {
    sched_entity_t* y = x->left;
    x->left = y->right;
    if (y->right != NIL) y->right->parent = x;
    y->parent = x->parent;
    if (x->parent == NIL) rq.root = y;
    else if (x == x->parent->right) x->parent->right = y;
    else x->parent->left = y;
    y->right = x; x->parent = y;
}
static void insert(sched_entity_t* z) {
    sched_entity_t *p = NIL, *n = rq.root;
    z->sequence = ++rq.sequence;
    while (n != NIL) { p = n; n = before(z, n) ? n->left : n->right; }
    z->parent = p; z->left = z->right = NIL; z->red = 1; z->in_tree = 1;
    if (p == NIL) rq.root = z;
    else if (before(z, p)) p->left = z;
    else p->right = z;
    while (z->parent->red) {
        if (z->parent == z->parent->parent->left) {
            sched_entity_t* uncle = z->parent->parent->right;
            if (uncle->red) {
                z->parent->red = uncle->red = 0;
                z->parent->parent->red = 1; z = z->parent->parent;
            } else {
                if (z == z->parent->right) { z = z->parent; rotate_left(z); }
                z->parent->red = 0; z->parent->parent->red = 1;
                rotate_right(z->parent->parent);
            }
        } else {
            sched_entity_t* uncle = z->parent->parent->left;
            if (uncle->red) {
                z->parent->red = uncle->red = 0;
                z->parent->parent->red = 1; z = z->parent->parent;
            } else {
                if (z == z->parent->left) { z = z->parent; rotate_right(z); }
                z->parent->red = 0; z->parent->parent->red = 1;
                rotate_left(z->parent->parent);
            }
        }
    }
    rq.root->red = 0;
    rq.leftmost = minimum(rq.root);
}
static void transplant(sched_entity_t* u, sched_entity_t* v) {
    if (u->parent == NIL) rq.root = v;
    else if (u == u->parent->left) u->parent->left = v;
    else u->parent->right = v;
    v->parent = u->parent;
}
static void remove_fixup(sched_entity_t* x) {
    while (x != rq.root && !x->red) {
        if (x == x->parent->left) {
            sched_entity_t* w = x->parent->right;
            if (w->red) {
                w->red = 0; x->parent->red = 1; rotate_left(x->parent);
                w = x->parent->right;
            }
            if (!w->left->red && !w->right->red) { w->red = 1; x = x->parent; }
            else {
                if (!w->right->red) {
                    w->left->red = 0; w->red = 1; rotate_right(w); w = x->parent->right;
                }
                w->red = x->parent->red; x->parent->red = 0; w->right->red = 0;
                rotate_left(x->parent); x = rq.root;
            }
        } else {
            sched_entity_t* w = x->parent->left;
            if (w->red) {
                w->red = 0; x->parent->red = 1; rotate_right(x->parent);
                w = x->parent->left;
            }
            if (!w->right->red && !w->left->red) { w->red = 1; x = x->parent; }
            else {
                if (!w->left->red) {
                    w->right->red = 0; w->red = 1; rotate_left(w); w = x->parent->left;
                }
                w->red = x->parent->red; x->parent->red = 0; w->left->red = 0;
                rotate_right(x->parent); x = rq.root;
            }
        }
    }
    x->red = 0;
}
static void remove_entity(sched_entity_t* z) {
    sched_entity_t *y = z, *x;
    uint8_t red = y->red;
    if (z->left == NIL) { x = z->right; transplant(z, z->right); }
    else if (z->right == NIL) { x = z->left; transplant(z, z->left); }
    else {
        y = minimum(z->right); red = y->red; x = y->right;
        if (y->parent == z) x->parent = y;
        else { transplant(y, y->right); y->right = z->right; y->right->parent = y; }
        transplant(z, y); y->left = z->left; y->left->parent = y; y->red = z->red;
    }
    if (!red) remove_fixup(x);
    z->in_tree = 0; z->left = z->right = z->parent = NIL;
    rq.leftmost = minimum(rq.root);
}
static void update_min(void) {
    uint64_t value = rq.min_vruntime;
    int valid = 0;
    if (rq.current && rq.current != rq.idle && rq.current->sched.runnable) {
        value = rq.current->sched.vruntime; valid = 1;
    }
    if (rq.leftmost != NIL && (!valid || rq.leftmost->vruntime < value))
        value = rq.leftmost->vruntime;
    if (value > rq.min_vruntime) rq.min_vruntime = value;
}
static void account(uint64_t now) {
    if (now < rq.now) now = rq.now;
    rq.now = now;
    if (!rq.current) return;
    sched_entity_t* se = &rq.current->sched;
    if (rq.current == rq.idle) scheduler_idle_time_ns += now - se->exec_start;
    if (rq.current != rq.idle && se->runnable) {
        uint64_t delta = now - se->exec_start;
        se->sum_exec_runtime += delta;
        se->vruntime += virtual_delta(delta, se->weight);
    }
    se->exec_start = now;
    update_min();
}
static uint64_t ideal_slice(sched_entity_t* se) {
    uint32_t period = rq.nr_running * SCHED_MIN_GRANULARITY_NS;
    if (period < SCHED_LATENCY_NS) period = SCHED_LATENCY_NS;
    uint64_t slice = divide((uint64_t)period * se->weight, rq.load ? rq.load : se->weight);
    return slice < SCHED_MIN_GRANULARITY_NS ? SCHED_MIN_GRANULARITY_NS : slice;
}
/* Re-evaluate the running slice when runnable load changes, rather than
 * waiting for the next timer IRQ. Callers still switch only at safe points. */
static void check_preempt(task_t* arriving) {
    if (rq.current == rq.idle) { rq.need_reschedule = 1; return; }
    if (!rq.current || !rq.current->sched.runnable) return;
    sched_entity_t* current = &rq.current->sched;
    if (rq.now - current->slice_start >= ideal_slice(current) ||
        current->vruntime > arriving->sched.vruntime +
        virtual_delta(SCHED_WAKEUP_GRANULARITY_NS, current->weight))
        rq.need_reschedule = 1;
}
void scheduler_init(task_t* boot, task_t* idle, uint64_t now) {
    rq.nil.left = rq.nil.right = rq.nil.parent = NIL; rq.nil.red = 0;
    rq.root = rq.leftmost = NIL;
    rq.current = boot; rq.idle = idle; rq.sleepers = 0;
    rq.now = now; rq.min_vruntime = rq.sequence = rq.switches = 0;
    scheduler_idle_time_ns = 0;
    rq.nr_running = 1; rq.load = 1024; rq.need_reschedule = 0;
    boot->sched.task = boot; boot->sched.weight = 1024; boot->sched.nice = 0;
    boot->sched.runnable = 1; boot->sched.in_tree = 0;
    boot->sched.vruntime = boot->sched.sum_exec_runtime = 0;
    boot->sched.exec_start = boot->sched.slice_start = now;
    boot->state = TASK_STATE_RUNNING;
    idle->sched.task = idle; idle->sched.runnable = idle->sched.in_tree = 0;
    idle->state = TASK_STATE_READY;
}
void scheduler_enqueue(task_t* t, int nice, uint64_t now) {
    /* Creation/reuse only; waking an existing task must preserve its history. */
    if (!t || t == rq.idle || t == rq.current ||
        (t->state != TASK_STATE_UNUSED && t->state != TASK_STATE_TERMINATED)) return;
    if (nice < -20) nice = -20;
    if (nice > 19) nice = 19;
    account(now);
    t->sched.task = t; t->sched.weight = scheduler_weight(nice); t->sched.nice = nice;
    t->sched.vruntime = rq.min_vruntime; t->sched.sum_exec_runtime = 0;
    t->sched.exec_start = t->sched.slice_start = now;
    t->sched.runnable = 1; t->sched.in_tree = 0; t->sched.sleep_next = 0;
    t->state = TASK_STATE_READY;
    rq.nr_running++; rq.load += t->sched.weight; insert(&t->sched);
    check_preempt(t);
}
static void unlink_sleep(task_t* t) {
    task_t** link = &rq.sleepers;
    while (*link && *link != t) link = &(*link)->sched.sleep_next;
    if (*link) *link = t->sched.sleep_next;
    t->sched.sleep_next = 0;
}
void scheduler_wake(task_t* t, uint64_t now) {
    if (!t || t == rq.idle || t->sched.runnable ||
        (t->state != TASK_STATE_BLOCKED && t->state != TASK_STATE_SLEEPING)) return;
    account(now);
    if (t->state == TASK_STATE_SLEEPING) unlink_sleep(t);
    uint64_t credit = virtual_delta(SCHED_SLEEPER_CREDIT_NS, t->sched.weight);
    uint64_t floor = rq.min_vruntime > credit ? rq.min_vruntime - credit : 0;
    if (t->sched.vruntime < floor) t->sched.vruntime = floor;
    t->sched.runnable = 1;
    /* A wake can cancel a wait before the task has scheduled away. Current
     * stays outside the tree; pick_next will enqueue it exactly once. */
    t->state = t == rq.current ? TASK_STATE_RUNNING : TASK_STATE_READY;
    startup_trace_event(TR_TASK_STATE, t->id, t->state, 0);
    rq.nr_running++; rq.load += t->sched.weight;
    if (t != rq.current) insert(&t->sched);
    check_preempt(t);
}
void scheduler_block(task_t* t, uint64_t now) {
    if (!t || t == rq.idle || t->state == TASK_STATE_UNUSED ||
        t->state == TASK_STATE_TERMINATED) return;
    account(now);
    /* Remove before changing state: wake/tick identify timed waits by state.
     * This also lets scheduler_sleep replace an existing deadline safely. */
    if (t->state == TASK_STATE_SLEEPING) unlink_sleep(t);
    if (t->sched.in_tree) remove_entity(&t->sched);
    if (t->sched.runnable) { rq.nr_running--; rq.load -= t->sched.weight; }
    t->sched.runnable = 0; t->state = TASK_STATE_BLOCKED;
    startup_trace_event(TR_TASK_STATE, t->id, TASK_STATE_BLOCKED, 0);
    if (t == rq.current) rq.need_reschedule = 1;
    update_min();
}
void scheduler_sleep(task_t* t, uint64_t deadline, uint64_t now) {
    if (!t || t == rq.idle || t->state == TASK_STATE_UNUSED ||
        t->state == TASK_STATE_TERMINATED) return;
    scheduler_block(t, now); t->state = TASK_STATE_SLEEPING; t->sched.wake_at = deadline;
    startup_trace_event(TR_TASK_STATE, t->id, TASK_STATE_SLEEPING, (uint32_t)(deadline-now));
    task_t** link = &rq.sleepers;
    while (*link && (*link)->sched.wake_at <= deadline) link = &(*link)->sched.sleep_next;
    t->sched.sleep_next = *link; *link = t;
}
void scheduler_exit(task_t* t, uint64_t now) {
    if (!t || t == rq.idle || t->state == TASK_STATE_UNUSED ||
        t->state == TASK_STATE_TERMINATED) return;
    scheduler_block(t, now); t->state = TASK_STATE_TERMINATED;
    startup_trace_event(TR_TASK_STATE,t->id,TASK_STATE_TERMINATED,0);
}
void scheduler_tick(uint64_t now) {
    account(now);
    while (rq.sleepers && rq.sleepers->sched.wake_at <= now) scheduler_wake(rq.sleepers, now);
    if (rq.current == rq.idle) { if (rq.nr_running) rq.need_reschedule = 1; return; }
    if (!rq.current || !rq.current->sched.runnable) { rq.need_reschedule = 1; return; }
    if (rq.leftmost == NIL) return;
    sched_entity_t* se = &rq.current->sched;
    uint64_t ran = now - se->slice_start;
    if (ran >= ideal_slice(se) || (ran >= SCHED_MIN_GRANULARITY_NS &&
        se->vruntime > rq.leftmost->vruntime + virtual_delta(SCHED_WAKEUP_GRANULARITY_NS, se->weight)))
        rq.need_reschedule = 1;
}
task_t* scheduler_pick_next(uint64_t now) {
    account(now);
    task_t* previous = rq.current;
    if (previous != rq.idle && previous->sched.runnable) {
        previous->state = TASK_STATE_READY; insert(&previous->sched);
    } else if (previous == rq.idle) previous->state = TASK_STATE_READY;
    if (rq.leftmost != NIL) {
        rq.current = rq.leftmost->task; remove_entity(&rq.current->sched);
    } else rq.current = rq.idle;
    rq.current->state = TASK_STATE_RUNNING;
    rq.current->sched.exec_start = rq.current->sched.slice_start = now;
    rq.need_reschedule = 0;
    if (previous != rq.current) rq.switches++;
    update_min();
    return rq.current;
}
int scheduler_need_reschedule(void) { return rq.need_reschedule; }
uint64_t scheduler_current_runtime(uint64_t now) {
    if (!rq.current || rq.current == rq.idle) return 0;
    sched_entity_t* se = &rq.current->sched;
    return se->sum_exec_runtime +
        (se->runnable && now > se->exec_start ? now - se->exec_start : 0);
}
void scheduler_set_nice(task_t* t, int nice, uint64_t now) {
    if (nice < -20) nice = -20;
    if (nice > 19) nice = 19;
    account(now);
    if (t == rq.idle) return;
    uint32_t weight = scheduler_weight(nice);
    if (t->sched.runnable) rq.load = rq.load - t->sched.weight + weight;
    t->sched.weight = weight; t->sched.nice = nice;
    rq.need_reschedule = 1;
}
void scheduler_get_info(scheduler_info_t* info) {
    if (!info) return;
    info->clock_ns = rq.now; info->min_vruntime = rq.min_vruntime;
    info->idle_time_ns = scheduler_idle_time_ns;
    info->switches = rq.switches; info->runnable = rq.nr_running; info->total_weight = rq.load;
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

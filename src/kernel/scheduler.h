#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

#include "types.h"
struct task;

#define SCHED_HZ 1000u
#define SCHED_LATENCY_NS 8000000u
#define SCHED_MIN_GRANULARITY_NS 1000000u
#define SCHED_WAKEUP_GRANULARITY_NS 500000u
#define SCHED_SLEEPER_CREDIT_NS 1000000u

typedef struct sched_entity {
    struct sched_entity *left, *right, *parent;
    struct task* task;
    struct task* sleep_next;
    uint64_t vruntime, sum_exec_runtime, exec_start, slice_start, wake_at, sequence;
    uint32_t weight;
    int nice;
    uint8_t red, in_tree, runnable;
} sched_entity_t;

typedef struct {
    uint64_t clock_ns, min_vruntime, switches, idle_time_ns;
    uint32_t runnable, total_weight;
} scheduler_info_t;

/* Policy entry points run with local IRQs disabled. The running entity is
 * excluded from the RB tree; runnable/load counts include it, excluding idle. */
void scheduler_init(struct task* boot, struct task* idle, uint64_t now);
void scheduler_enqueue(struct task* task, int nice, uint64_t now);
void scheduler_wake(struct task* task, uint64_t now);
void scheduler_sleep(struct task* task, uint64_t deadline, uint64_t now);
void scheduler_block(struct task* task, uint64_t now);
void scheduler_exit(struct task* task, uint64_t now);
void scheduler_tick(uint64_t now);
struct task* scheduler_pick_next(uint64_t now);
int scheduler_need_reschedule(void);
void scheduler_set_nice(struct task* task, int nice, uint64_t now);
void scheduler_get_info(scheduler_info_t* info);
uint32_t scheduler_weight(int nice);

/* Platform integration: 1 kHz PIT, preserving the existing 100 Hz app clock. */
uint64_t scheduler_clock(void);
void scheduler_start(void);
int scheduler_running(void);
void scheduler_irq_tick(void);

#endif

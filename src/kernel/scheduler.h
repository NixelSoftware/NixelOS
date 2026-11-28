#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

#include "types.h"
struct task;

#define SCHED_HZ 1000u
/* One PIT quantum: a 4ms CPU request delayed input behind valid earlier
 * deadlines under mixed load. Weight still determines long-term CPU share. */
#define SCHED_REQUEST_NS 1000000u
#define SCHED_INTERACTIVE_REQUEST_NS 1000000u

typedef struct sched_entity {
    /* Published policy metadata. Authoritative EEVDF state lives in the fixed
     * run-queue slots; queued includes delayed sleepers, runnable does not. */
    uint32_t weight;
    int nice;
    uint8_t queued, runnable, policy_member, delayed;
} sched_entity_t;

typedef struct {
    uint64_t clock_ns, switches, idle_time_ns;
    uint32_t runnable, total_weight;
} scheduler_info_t;

/* Policy entry points run with local IRQs disabled. A fixed array covers the
 * 32 task slots. Runnable/load counts exclude idle and delayed sleepers. */
void scheduler_init(struct task* boot, struct task* idle, uint64_t now);
/* Enqueue initializes unused/terminated slots only, excluding current/idle.
 * Wake accepts blocked/sleeping tasks only. Repeated sleep replaces a timed
 * wait; block cancels it. Waking current before it schedules away cancels the
 * wait without queuing current. Invalid transitions are ignored. */
void scheduler_enqueue(struct task* task, int nice, uint64_t now);
void scheduler_enqueue_request(struct task* task, int nice, uint32_t request_ns, uint64_t now);
void scheduler_wake(struct task* task, uint64_t now);
void scheduler_sleep(struct task* task, uint64_t deadline, uint64_t now);
void scheduler_block(struct task* task, uint64_t now);
void scheduler_exit(struct task* task, uint64_t now);
void scheduler_tick(uint64_t now);
struct task* scheduler_pick_next(uint64_t now);
int scheduler_need_reschedule(void);
/* Task creation must not overwrite a retained policy or current stack slot. */
int scheduler_can_reuse(const struct task* task);
void scheduler_set_nice(struct task* task, int nice, uint64_t now);
void scheduler_get_info(scheduler_info_t* info);
uint32_t scheduler_weight(int nice);
/* With IRQs disabled, sample scheduled runtime at the supplied clock time. */
uint64_t scheduler_current_runtime(uint64_t now);

/* Platform integration: 1 kHz PIT, preserving the existing 100 Hz app clock. */
uint64_t scheduler_clock(void);
void scheduler_start(void);
int scheduler_running(void);
void scheduler_irq_tick(void);

#endif

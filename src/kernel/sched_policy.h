#ifndef EEVDF_POLICY_H
#define EEVDF_POLICY_H
#include "sched_wide.h"
#define EE_TASKS 32
#define EE_BOUND (EE_I64_C(1) << 60)
enum { EE_UNUSED, EE_READY, EE_RUNNING, EE_BLOCKED, EE_SLEEPING, EE_DEAD };
enum { EE_CREATE = 1, EE_TICK, EE_PICK, EE_BLOCK, EE_SLEEP, EE_WAKE,
       EE_EXIT, EE_NICE, EE_YIELD, EE_REMOVE };
enum { EE_OK, EE_INVALID, EE_RANGE };
typedef struct {
    int64_t v, d, lag, relative;
    uint64_t runtime, wake_at;
    uint32_t state, weight, slice, remainder;
    uint32_t member, delayed, protected_request, relative_valid;
} ee_entity;
typedef struct {
    ee_entity task[EE_TASKS];
    uint64_t now;
    int32_t current;
    uint32_t need_reschedule;
} ee_queue;

void ee_init(ee_queue *q);
uint32_t ee_weight(int nice);
int ee_eligible(const ee_queue *q, unsigned id);
/* Pure policy transaction; invalid/range failures leave q byte-for-byte intact.
 * SLEEP's value is a relative duration in ns (zero means 1 ms).
 * CREATE uses arg=nice, value=slice; NICE uses arg=nice. All times are explicit.
 * The production queue scans the bounded 32-slot array; current is considered
 * once, and delayed sleepers participate without becoming executable. */
int ee_apply(ee_queue *q, unsigned op, unsigned id, int arg, uint64_t value, uint64_t now);
#endif

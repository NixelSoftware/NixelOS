#ifndef TASK_H
#define TASK_H
#include "types.h"
#include "scheduler.h"
#include "startup_trace.h"
#define MAX_TASKS 32
#define TASK_STACK_SIZE 32768
#define TASK_STATE_UNUSED 0
#define TASK_STATE_READY 1
#define TASK_STATE_RUNNING 2
#define TASK_STATE_TERMINATED 3
#define TASK_STATE_SLEEPING 4
#define TASK_STATE_BLOCKED 5

typedef struct task {
    uint32_t id, state, esp; /* esp at offset 8 is shared with task_switch.asm. */
    uint32_t directory, user_kernel_esp, user_exit_requested;
    uint32_t preempt_count;
    int base_nice;
    struct mutex *owned_mutexes, *waiting_mutex;
    struct task* wait_next;
    sched_entity_t sched;
    uint8_t fpu_state[108] __attribute__((aligned(16)));
    uint8_t stack[TASK_STACK_SIZE] __attribute__((aligned(16)));
    char name[16];
} task_t;
void tasking_init(void);
int task_create(void (*entry)(void*), void* arg, const char* name);
int task_create_nice(void (*entry)(void*), void* arg, const char* name, int nice);
void task_exit(void) __attribute__((noreturn));
void task_yield(void);
void task_sleep(uint32_t legacy_ticks);
void task_sleep_ms(uint32_t milliseconds);
void task_preempt_disable(void);
void task_preempt_enable(void);
void task_preempt_point(void);
void task_timer_tick(uint32_t interrupted_cs);
void task_schedule_next(void);
extern task_t* current_task;
static inline uint32_t task_irq_save(void) {
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    if (flags & 0x200u) startup_trace_irq_begin((uint32_t)__builtin_return_address(0));
    return flags;
}
static inline void task_irq_restore(uint32_t flags) {
    if (flags & 0x200u) startup_trace_irq_end();
    __asm__ volatile ("pushl %0; popfl" : : "r"(flags) : "memory", "cc");
}
#endif

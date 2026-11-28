#ifndef TASK_H
#define TASK_H
#include "types.h"
#include "scheduler.h"
#include "arch/arch.h"
#include "arch_context.h"
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
    struct arch_context arch;
    uint32_t id, state;
    uint32_t directory, preempt_count;
    uint32_t parent_id, generation, terminate_requested, detached;
    uint16_t uid, gid;
    struct terminal* terminal;
    char command_args[256];
    char cwd[256]; /* absolute working directory, inherited by child tasks */
    void (*shell_output)(const char*);
    void (*shell_clear)(void);
    int base_nice;
    struct mutex *owned_mutexes, *waiting_mutex;
    struct task* wait_next;
    sched_entity_t sched;
    uint8_t stack[TASK_STACK_SIZE] __attribute__((aligned(16)));
    char name[16];
} task_t;
_Static_assert(__builtin_offsetof(task_t, arch.esp) == 0, "assembly task ESP offset");
void tasking_init(void);
int task_create(void (*entry)(void*), void* arg, const char* name);
int task_create_nice(void (*entry)(void*), void* arg, const char* name, int nice);
/* Kernel service latency request, independent of its long-term nice weight.
 * Request range is 1..16 ms; ordinary tasks use the one-tick default. */
int task_create_with_request(void (*entry)(void*), void* arg, const char* name,
                             int nice, uint32_t request_ns);
/* Current-task exit only; mutex ownership/waits and preempt-disable nesting
 * must be cleared by the caller. Violations trap rather than abandon locks. */
void task_exit(void) __attribute__((noreturn));
void task_yield(void);
int task_wait(int pid); /* waits until child exits or detaches; no locks held */
int task_detach_child(int pid);
int task_detach(void); /* current process reparents itself to init */
void task_terminate_children(uint32_t parent);
uint32_t task_parent_id(void);
typedef struct {
    uint32_t pid, ppid, state;
    char name[16];
} task_info_t;
int task_snapshot(task_info_t* output, int capacity);
int task_cwd_in_use(const char* path);
void task_sleep(uint32_t legacy_ticks);
void task_sleep_ms(uint32_t milliseconds);
/* One-waiter wakeup latch. A signal with no waiter is kept for the next wait. */
typedef struct { task_t* waiter; uint8_t pending; } task_event_t;
/* Returns on signal or timeout, whichever comes first. */
void task_event_wait_ms(task_event_t* event, uint32_t milliseconds);
/* Safe from IRQ handlers and tasks. */
void task_event_signal(task_event_t* event);
void task_preempt_disable(void);
void task_preempt_enable(void);
void task_preempt_point(void);
void task_timer_tick(void);
/* Timer return only: saved frame exists and local IRQs remain disabled. */
void task_timer_irq_exit(void);
void task_schedule_next(void);
int task_kill(uint32_t pid);
/* Snapshot of active CR3 and suspended live task contexts. Callers must also
 * serialize creation of new references for the duration of reclamation. */
int task_address_space_in_use(uint32_t directory);
extern task_t* current_task;
static inline uint32_t task_irq_save(void) {
    uint32_t flags = arch_irq_save();
    if (arch_irq_were_enabled(flags))
        startup_trace_irq_begin((uint32_t)__builtin_return_address(0));
    return flags;
}
static inline void task_irq_restore(uint32_t flags) {
    if (arch_irq_were_enabled(flags)) startup_trace_irq_end();
    arch_irq_restore(flags);
}
#endif

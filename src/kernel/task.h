#ifndef TASK_H
#define TASK_H
#include "types.h"
#include "scheduler.h"
#include "arch_fpu.h"
#include "startup_trace.h"
#define MAX_TASKS 32
#define TASK_STACK_SIZE 32768
#define TASK_STATE_UNUSED 0
#define TASK_STATE_READY 1
#define TASK_STATE_RUNNING 2
#define TASK_STATE_TERMINATED 3
#define TASK_STATE_SLEEPING 4
#define TASK_STATE_BLOCKED 5

/* task_switch.asm timer/yield ABI, NOT the exception/syscall registers_t.
 * Hardware appends user ESP/SS only when returning across privilege levels. */
typedef struct task_switch_frame {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t eip, cs, eflags;
} task_switch_frame_t;
_Static_assert(sizeof(task_switch_frame_t) == 60, "switch frame size");
_Static_assert(__builtin_offsetof(task_switch_frame_t, edi) == 16, "PUSHAD offset");
_Static_assert(__builtin_offsetof(task_switch_frame_t, eip) == 48, "IRET offset");
_Static_assert(__builtin_offsetof(task_switch_frame_t, cs) == 52, "saved CS offset");
_Static_assert(__builtin_offsetof(task_switch_frame_t, eflags) == 56, "saved flags offset");
typedef struct {
    task_switch_frame_t kernel;
    uint32_t useresp, ss;
} task_user_switch_frame_t;
_Static_assert(sizeof(task_user_switch_frame_t) == 68, "user switch frame size");
_Static_assert(__builtin_offsetof(task_user_switch_frame_t, useresp) == 60, "user ESP offset");

typedef struct task {
    uint32_t id, state, esp; /* esp at offset 8 is shared with task_switch.asm. */
    uint32_t directory, user_kernel_esp, user_exit_requested;
    uint32_t parent_id, generation, terminate_requested, detached;
    uint16_t uid, gid;
    struct terminal* terminal;
    char command_args[256];
    void (*shell_output)(const char*);
    void (*shell_clear)(void);
    uint32_t preempt_count;
    int base_nice;
    struct mutex *owned_mutexes, *waiting_mutex;
    struct task* wait_next;
    sched_entity_t sched;
    arch_fpu_state_t fpu_state;
    uint8_t stack[TASK_STACK_SIZE] __attribute__((aligned(16)));
    char name[16];
} task_t;
_Static_assert(__builtin_offsetof(task_t, esp) == 8, "assembly task ESP offset");
_Static_assert(__builtin_offsetof(task_t, fpu_state) % 16 == 0, "task FPU alignment");
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
int task_detach(void); /* current process reparents itself to init */
void task_terminate_children(uint32_t parent);
uint32_t task_parent_id(void);
typedef struct {
    uint32_t pid, ppid, state;
    char name[16];
} task_info_t;
int task_snapshot(task_info_t* output, int capacity);
void task_sleep(uint32_t legacy_ticks);
void task_sleep_ms(uint32_t milliseconds);
void task_preempt_disable(void);
void task_preempt_enable(void);
void task_preempt_point(void);
void task_timer_tick(void);
/* Timer return only: saved frame exists and local IRQs remain disabled. */
void task_timer_irq_exit(void);
void task_schedule_next(void);
/* Snapshot of active CR3 and suspended live task contexts. Callers must also
 * serialize creation of new references for the duration of reclamation. */
int task_address_space_in_use(uint32_t directory);
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

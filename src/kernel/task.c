#include "task.h"
#include "paging.h"
#include "arch_i386.h"

extern uint32_t arch_user_kernel_esp;
static task_t task_list[MAX_TASKS];
task_t* current_task;
static arch_fpu_state_t initial_fpu_state;
static uint32_t next_generation;

int task_address_space_in_use(uint32_t directory) {
    if (!directory) return 0;
    uint32_t flags = task_irq_save();
    int used = paging_current_directory() == directory;
    for (int i = 0; !used && i < MAX_TASKS; i++) {
        const task_t* t = &task_list[i];
        /* Current's saved directory can be stale after an explicit CR3 change;
         * its actual directory was checked above. Dead tasks cannot resume. */
        if (t != current_task && t->state != TASK_STATE_UNUSED &&
            t->state != TASK_STATE_TERMINATED && t->directory == directory)
            used = 1;
    }
    task_irq_restore(flags);
    return used;
}

/* IRQs are disabled by the assembly switch. Policy lives in scheduler.c. */
void task_schedule_next(void) {
    if (!current_task || !scheduler_running()) return;
    task_t* previous = current_task;
    task_t* next = scheduler_pick_next(scheduler_clock());
    if (previous == next) return;
    startup_trace_switch(previous->id, next->id);
    arch_fpu_save(&previous->fpu_state);
    previous->directory = paging_current_directory();
    previous->user_kernel_esp = arch_user_kernel_esp;
    previous->user_exit_requested = arch_user_exit_requested;
    current_task = next;
    arch_fpu_restore(&next->fpu_state);
    arch_user_kernel_esp = next->user_kernel_esp;
    arch_user_exit_requested = next->user_exit_requested |
        (next->terminate_requested && next->user_kernel_esp);
    arch_i386_set_kernel_stack(arch_user_kernel_esp ? arch_user_kernel_esp :
        (uint32_t)(next->stack + TASK_STACK_SIZE));
    if (paging_current_directory() != next->directory)
        paging_activate_directory(next->directory);
}
void task_timer_tick(void) {
    scheduler_irq_tick();
    if (!current_task || !scheduler_running()) return;
    if (current_task->terminate_requested && arch_user_kernel_esp)
        arch_user_exit_requested = 1;
    scheduler_tick(scheduler_clock());
}
void task_timer_irq_exit(void) {
    if (!current_task || !scheduler_running()) return;
    /* Kernel threads and syscall work are preemptible too. Shared state is
     * protected by sleeping mutexes; only tiny atomic regions defer switches. */
    if (!current_task->preempt_count && scheduler_need_reschedule()) task_schedule_next();
}
void task_preempt_disable(void) {
    if (current_task) current_task->preempt_count++;
    __asm__ volatile ("" : : : "memory");
}
void task_preempt_enable(void) {
    __asm__ volatile ("" : : : "memory");
    if (current_task && current_task->preempt_count) current_task->preempt_count--;
    task_preempt_point();
}
void task_preempt_point(void) {
    if (!current_task || !scheduler_running() || current_task->preempt_count ||
        !scheduler_need_reschedule()) return;
    uint32_t flags = task_irq_save();
    if (flags & 0x200u) task_yield();
    task_irq_restore(flags);
}
void task_sleep_ms(uint32_t milliseconds) {
    if (!current_task || !scheduler_running() || current_task->preempt_count) return;
    uint32_t flags = task_irq_save();
    uint64_t now = scheduler_clock();
    uint64_t duration = (uint64_t)(milliseconds ? milliseconds : 1u) * 1000000u;
    if (duration > ~(uint64_t)0 - now) __builtin_trap();
    scheduler_sleep(current_task, now + duration, now);
    task_yield();
    task_irq_restore(flags);
}
void task_sleep(uint32_t ticks) {
    task_sleep_ms(ticks > 0x0fffffffu ? 0x7fffffffu : ticks * 10u);
}
void task_exit(void) {
    __asm__ volatile ("cli" : : : "memory");
    /* Dropping locks here could publish partially updated shared state.
     * Report a kernel contract violation rather than strand lock waiters. */
    if (current_task->owned_mutexes || current_task->waiting_mutex ||
        current_task->wait_next || current_task->preempt_count)
        __asm__ volatile ("ud2");
    if (!current_task->id) __builtin_trap();
    task_terminate_children(current_task->id);
    for (int i = 1; i < MAX_TASKS - 1; i++)
        if (i != (int)current_task->id) task_wait(i);
    scheduler_exit(current_task, scheduler_clock());
    task_yield();
    for (;;) __asm__ volatile ("sti; hlt");
}
uint32_t task_parent_id(void) {
    return current_task ? current_task->parent_id : 0;
}
/* Requests propagate atomically, including through sleeping app loaders.
 * User return boundaries unwind the app stack and run its normal cleanup. */
void task_terminate_children(uint32_t parent) {
    uint32_t flags = task_irq_save();
    for (int i = 1; i < MAX_TASKS - 1; i++) {
        task_t* child = &task_list[i];
        if (child->parent_id != parent || child->state == TASK_STATE_UNUSED ||
            child->state == TASK_STATE_TERMINATED) continue;
        child->terminate_requested = 1;
        if (child == current_task && arch_user_kernel_esp) arch_user_exit_requested = 1;
        task_terminate_children(child->id);
    }
    task_irq_restore(flags);
}
int task_detach(void) {
    uint32_t flags = task_irq_save();
    int result = -1;
    if (current_task && !current_task->terminate_requested) {
        current_task->parent_id = 0;
        current_task->detached = 1;
        current_task->terminal = 0;
        result = 0;
    }
    task_irq_restore(flags);
    return result;
}
int task_wait(int pid) {
    if (!current_task || pid <= 0 || pid >= MAX_TASKS - 1) return -1;
    uint32_t flags = task_irq_save();
    task_t* child = &task_list[pid];
    uint32_t generation = child->generation;
    if (child->parent_id != current_task->id) {
        task_irq_restore(flags);
        return -1;
    }
    while (child->generation == generation && child->parent_id == current_task->id &&
           child->state != TASK_STATE_TERMINATED && child->state != TASK_STATE_UNUSED) {
        task_irq_restore(flags);
        task_sleep_ms(1);
        flags = task_irq_save();
    }
    task_irq_restore(flags);
    return 0;
}
static void task_return_stub(void) { task_exit(); }
static void idle_main(void* arg) {
    (void)arg;
    for (;;) {
        __asm__ volatile ("cli" : : : "memory");
        if (scheduler_need_reschedule()) {
            task_yield();
            __asm__ volatile ("sti" : : : "memory");
        } else {
            /* STI's interrupt shadow keeps the check-to-halt boundary safe. */
            __asm__ volatile ("sti; hlt" : : : "memory");
        }
    }
}
static void setup(task_t* t, void (*entry)(void*), void* arg, const char* name) {
    t->uid = current_task ? current_task->uid : 0;
    t->gid = current_task ? current_task->gid : 0;
    t->parent_id = current_task ? current_task->id : 0;
    t->generation = ++next_generation;
    t->terminate_requested = 0;
    t->detached = current_task ? current_task->detached : 0;
    t->terminal = current_task ? current_task->terminal : 0;
    t->shell_output = current_task ? current_task->shell_output : 0;
    t->shell_clear = current_task ? current_task->shell_clear : 0;
    for (unsigned i = 0; i < sizeof(t->command_args); i++)
        t->command_args[i] = current_task ? current_task->command_args[i] : 0;
    t->directory = paging_kernel_directory();
    t->user_kernel_esp = t->user_exit_requested = t->preempt_count = 0;
    t->wait_next = 0;
    t->owned_mutexes = t->waiting_mutex = 0;
    t->base_nice = 0;
    t->fpu_state = initial_fpu_state;
    int n = 0;
    while (name && name[n] && n < 15) { t->name[n] = name[n]; n++; }
    t->name[n] = 0;
    uint32_t* sp = (uint32_t*)((uint32_t)(t->stack + TASK_STACK_SIZE) & ~0xFu);
    sp -= 3;
    *(--sp) = (uint32_t)arg; *(--sp) = (uint32_t)task_return_stub;
    task_switch_frame_t* frame = (task_switch_frame_t*)((uint8_t*)sp - sizeof(*frame));
    *frame = (task_switch_frame_t){
        .gs = 0x10, .fs = 0x10, .es = 0x10, .ds = 0x10,
        .eip = (uint32_t)entry, .cs = 0x08, .eflags = 0x202
    };
    t->esp = (uint32_t)frame;
}
static int slot_reusable(const task_t* t) {
    /* setup overwrites the stack and linkage. Never do that while any of
     * these references still belong to the old execution context. IRQs off. */
    return t != current_task &&
        (t->state == TASK_STATE_UNUSED || t->state == TASK_STATE_TERMINATED) &&
        !t->owned_mutexes && !t->waiting_mutex && !t->wait_next &&
        !t->sched.runnable && !t->sched.queued && !t->sched.policy_member &&
        !t->sched.delayed && scheduler_can_reuse(t);
}
int task_create_nice(void (*entry)(void*), void* arg, const char* name, int nice) {
    return task_create_with_request(entry, arg, name, nice, SCHED_REQUEST_NS);
}
int task_create_with_request(void (*entry)(void*), void* arg, const char* name,
                             int nice, uint32_t request_ns) {
    if (!entry || request_ns < 1000000u || request_ns > 16000000u) return -1;
    uint32_t flags = task_irq_save();
    if (current_task && current_task->terminate_requested) {
        task_irq_restore(flags);
        return -1;
    }
    int slot = -1;
    for (int i = 1; i < MAX_TASKS - 1; i++) {
        if (slot_reusable(&task_list[i])) {
            slot = i; break;
        }
    }
    if (slot >= 0) {
        setup(&task_list[slot], entry, arg, name);
        task_list[slot].base_nice = nice < -20 ? -20 : nice > 19 ? 19 : nice;
        scheduler_enqueue_request(&task_list[slot], task_list[slot].base_nice,
                                  request_ns, scheduler_clock());
    }
    task_irq_restore(flags);
    task_preempt_point();
    return slot;
}
int task_create(void (*entry)(void*), void* arg, const char* name) {
    return task_create_nice(entry, arg, name, 0);
}
void tasking_init(void) {
    if (!arch_fpu_init(&initial_fpu_state)) __builtin_trap();
    for (int i = 0; i < MAX_TASKS; i++) { task_list[i].state = TASK_STATE_UNUSED; task_list[i].id = i; }
    current_task = &task_list[0];
    setup(current_task, idle_main, 0, "init");
    task_t* idle = &task_list[MAX_TASKS - 1];
    setup(idle, idle_main, 0, "idle");
    scheduler_init(current_task, idle, 0);
    current_task->base_nice = -5;
    scheduler_set_nice(current_task, -5, 0);
}

int task_snapshot(task_info_t* output, int capacity) {
    if (!output || capacity < 0) return -1;
    uint32_t flags = task_irq_save();
    int count = 0;
    for (int i = 0; i < MAX_TASKS - 1 && count < capacity; i++) {
        task_t* t = &task_list[i];
        if (t->state == TASK_STATE_UNUSED || t->state == TASK_STATE_TERMINATED) continue;
        output[count].pid = t->id;
        output[count].ppid = t->parent_id;
        output[count].state = t->state;
        for (int n = 0; n < 16; n++) output[count].name[n] = t->name[n];
        count++;
    }
    task_irq_restore(flags);
    return count;
}

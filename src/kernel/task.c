#include "task.h"
#include "paging.h"
#include "arch_i386.h"

extern uint32_t arch_user_kernel_esp;
static task_t task_list[MAX_TASKS];
task_t* current_task;
static uint8_t initial_fpu_state[108] __attribute__((aligned(16)));

/* IRQs are disabled by the assembly switch. Policy lives in scheduler.c. */
void task_schedule_next(void) {
    if (!current_task || !scheduler_running()) return;
    task_t* previous = current_task;
    task_t* next = scheduler_pick_next(scheduler_clock());
    if (previous == next) return;
    __asm__ volatile ("fnsave %0" : "=m"(previous->fpu_state));
    previous->directory = paging_current_directory();
    previous->user_kernel_esp = arch_user_kernel_esp;
    previous->user_exit_requested = arch_user_exit_requested;
    current_task = next;
    __asm__ volatile ("frstor %0" : : "m"(next->fpu_state));
    arch_user_kernel_esp = next->user_kernel_esp;
    arch_user_exit_requested = next->user_exit_requested;
    arch_i386_set_kernel_stack(arch_user_kernel_esp ? arch_user_kernel_esp :
        (uint32_t)(next->stack + TASK_STACK_SIZE));
    if (paging_current_directory() != next->directory)
        paging_activate_directory(next->directory);
}
void task_timer_tick(uint32_t interrupted_cs) {
    (void)interrupted_cs;
    scheduler_irq_tick();
    if (!current_task || !scheduler_running()) return;
    scheduler_tick(scheduler_clock());
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
    scheduler_sleep(current_task, now + (uint64_t)(milliseconds ? milliseconds : 1u) * 1000000u, now);
    task_yield();
    task_irq_restore(flags);
}
void task_sleep(uint32_t ticks) {
    task_sleep_ms(ticks > 0x0fffffffu ? 0x7fffffffu : ticks * 10u);
}
void task_exit(void) {
    __asm__ volatile ("cli" : : : "memory");
    scheduler_exit(current_task, scheduler_clock());
    task_yield();
    for (;;) __asm__ volatile ("sti; hlt");
}
static void task_return_stub(void) { task_exit(); }
static void idle_main(void* arg) {
    (void)arg;
    for (;;) __asm__ volatile ("sti; hlt" : : : "memory");
}
static void setup(task_t* t, void (*entry)(void*), void* arg, const char* name) {
    t->directory = paging_kernel_directory();
    t->user_kernel_esp = t->user_exit_requested = t->preempt_count = 0;
    t->wait_next = 0;
    t->owned_mutexes = t->waiting_mutex = 0;
    t->base_nice = 0;
    for (uint32_t i = 0; i < sizeof(initial_fpu_state); i++) t->fpu_state[i] = initial_fpu_state[i];
    int n = 0;
    while (name && name[n] && n < 15) { t->name[n] = name[n]; n++; }
    t->name[n] = 0;
    uint32_t* sp = (uint32_t*)((uint32_t)(t->stack + TASK_STACK_SIZE) & ~0xFu);
    sp -= 3;
    *(--sp) = (uint32_t)arg; *(--sp) = (uint32_t)task_return_stub;
    *(--sp) = 0x202; *(--sp) = 0x08; *(--sp) = (uint32_t)entry;
    for (int i = 0; i < 8; i++) *(--sp) = 0;
    for (int i = 0; i < 4; i++) *(--sp) = 0x10;
    t->esp = (uint32_t)sp;
}
int task_create_nice(void (*entry)(void*), void* arg, const char* name, int nice) {
    if (!entry) return -1;
    uint32_t flags = task_irq_save();
    int slot = -1;
    for (int i = 1; i < MAX_TASKS - 1; i++) {
        if (task_list[i].state == TASK_STATE_UNUSED || task_list[i].state == TASK_STATE_TERMINATED) {
            slot = i; break;
        }
    }
    if (slot >= 0) {
        setup(&task_list[slot], entry, arg, name);
        task_list[slot].base_nice = nice < -20 ? -20 : nice > 19 ? 19 : nice;
        scheduler_enqueue(&task_list[slot], task_list[slot].base_nice, scheduler_clock());
    }
    task_irq_restore(flags);
    return slot;
}
int task_create(void (*entry)(void*), void* arg, const char* name) {
    return task_create_nice(entry, arg, name, 0);
}
void tasking_init(void) {
    __asm__ volatile ("fninit; fnsave %0" : "=m"(initial_fpu_state));
    __asm__ volatile ("frstor %0" : : "m"(initial_fpu_state));
    for (int i = 0; i < MAX_TASKS; i++) { task_list[i].state = TASK_STATE_UNUSED; task_list[i].id = i; }
    current_task = &task_list[0];
    setup(current_task, idle_main, 0, "desktop");
    task_t* idle = &task_list[MAX_TASKS - 1];
    setup(idle, idle_main, 0, "idle");
    scheduler_init(current_task, idle, 0);
    current_task->base_nice = -5;
    scheduler_set_nice(current_task, -5, 0);
}

/* Real i386 PIT/IRET tests. Paging and TSS are intentionally outside this test. */
#include "task.h"
#include "paging.h"
#include "arch_i386.h"

uint32_t arch_user_kernel_esp;
volatile uint32_t arch_user_exit_requested, frame_timer;
static volatile uint32_t ticks, worker_runs, stop_worker, worker_done;
static uint32_t timer_active, idle_entries, idle_exits;
static struct __attribute__((packed)) { uint16_t low, cs; uint8_t zero, type; uint16_t high; } idt[256];
extern void irq0_timer_handler(void);

static uint32_t flags(void) {
    uint32_t f;
    __asm__ volatile ("pushfl; popl %0" : "=r"(f));
    return f;
}
static void print(const char* s) { while (*s) outb(0xe9, *s++); }
static void check(int ok, const char* message) {
    if (ok) return;
    __asm__ volatile ("cli");
    print("FAIL: "); print(message); print("\n");
    outb(0xf4, 1);
    for (;;) __asm__ volatile ("hlt");
}
uint32_t paging_kernel_directory(void) { return 0x1000; }
uint32_t paging_current_directory(void) { return 0x1000; }
void paging_activate_directory(uint32_t directory) { check(directory == 0x1000, "directory"); }
void arch_i386_set_kernel_stack(uint32_t top) { check(top != 0, "kernel stack"); }
void startup_trace_irq_begin(uint32_t caller) { (void)caller; }
void startup_trace_irq_end(void) {}
void startup_trace_clock(uint32_t source, uint32_t rate) {
    (void)rate;
    print(source == 1 ? "Clock: TSC\n" : "Clock: PIT\n");
}
void startup_trace_event(uint32_t e, uint32_t a, uint32_t b, uint32_t c) {
    (void)e; (void)a; (void)b; (void)c;
}
void startup_trace_switch(uint32_t previous, uint32_t next) {
    check(!(flags() & 0x200), "switch with IRQs enabled");
    check(!timer_active, "switch before timer tracing completes");
    check(!current_task->preempt_count, "switch inside preempt-disabled region");
    if (next == MAX_TASKS - 1) idle_entries++;
    if (previous == MAX_TASKS - 1) idle_exits++;
}
void startup_trace_timer(int entering) {
    check(!(flags() & 0x200), "timer IF");
    check(timer_active != (uint32_t)entering, "nested/unbalanced timer");
    timer_active = entering;
    if (entering) ticks++;
}
static void wait_ticks(uint32_t count) {
    uint32_t start = ticks;
    while (ticks - start < count) __asm__ volatile ("hlt" : : : "memory");
}
static void worker(void* arg) {
    (void)arg;
    while (!stop_worker) {
        worker_runs++;
        task_sleep_ms(1);
    }
    worker_done = 1;
}
void test_main(void) {
    print("Starting multitasking boundary tests\n");
    uint32_t handler = (uint32_t)irq0_timer_handler;
    idt[32].low = handler; idt[32].high = handler >> 16;
    idt[32].cs = 8; idt[32].type = 0x8e;
    struct __attribute__((packed)) { uint16_t limit; uint32_t base; } idtr = { sizeof(idt)-1, (uint32_t)idt };
    __asm__ volatile ("lidt %0" : : "m"(idtr));
    outb(0x20, 0x11); outb(0xa0, 0x11);
    outb(0x21, 0x20); outb(0xa1, 0x28);
    outb(0x21, 4); outb(0xa1, 2);
    outb(0x21, 1); outb(0xa1, 1);
    outb(0x21, 0xff); outb(0xa1, 0xff);
    tasking_init();
    scheduler_start();
    print("PIT started\n");
    task_t* boot = current_task;
    check(task_create(worker, 0, "boundary-worker") > 0, "worker creation");
    task_preempt_disable(); task_preempt_disable();
    __asm__ volatile ("sti");
    uint32_t before = worker_runs;
    wait_ticks(20);
    check(current_task == boot && worker_runs == before, "nested deferral");
    check(scheduler_need_reschedule(), "timer must request rescheduling");
    task_preempt_enable();
    wait_ticks(5);
    check(boot->preempt_count == 1 && worker_runs == before, "inner enable");
    task_preempt_enable();
    check(boot->preempt_count == 0 && worker_runs > before, "outer enable must schedule");
    check(flags() & 0x200, "outer enable restores IF");
    print("PASS nested timer preemption\n");

    task_preempt_disable();
    wait_ticks(20);
    uint32_t outer = task_irq_save();
    uint32_t inner = task_irq_save();
    before = worker_runs;
    task_preempt_enable();
    task_preempt_point();
    check(!boot->preempt_count && scheduler_need_reschedule(), "masked enable retains request");
    check(!(flags() & 0x200) && worker_runs == before, "masked preemption point");
    task_irq_restore(inner);
    check(!(flags() & 0x200), "inner restore enables IRQs");
    task_irq_restore(outer);
    wait_ticks(5);
    check(worker_runs > before && (flags() & 0x200), "timer return services deferred request");
    print("PASS IRQ-disabled boundaries\n");

    stop_worker = 1;
    while (!worker_done) task_sleep_ms(2);
    /* worker_done is published before task_exit; let its final context retire. */
    task_sleep_ms(3);
    /* A voluntary sleep may start masked and must resume with that same IF. */
    uint32_t masked = task_irq_save();
    task_sleep_ms(2);
    check(!(flags() & 0x200), "masked sleep restores IF");
    task_irq_restore(masked);
    uint32_t irq = task_irq_save();
    scheduler_info_t initial, final;
    scheduler_get_info(&initial);
    task_irq_restore(irq);
    uint32_t entries = idle_entries, exits = idle_exits;
    for (uint32_t i = 0; i < 128; i++) {
        task_sleep_ms(i % 3); /* zero is a one-millisecond sleep */
        check(current_task == boot && (flags() & 0x200), "idle wake context/IF");
        check(!boot->sched.in_tree && boot->sched.runnable, "wake queue membership");
    }
    irq = task_irq_save();
    scheduler_get_info(&final);
    check(idle_entries - entries == 128 && idle_exits - exits == 128, "idle entry/exit count");
    check(final.idle_time_ns > initial.idle_time_ns, "halted time accounting");
    check(final.runnable == 1 && final.total_weight == scheduler_weight(-5), "idle excluded from load");
    check(!timer_active, "timer trace balanced");
    print("PASS 128 idle sleep/wake transitions\nPASS multitasking boundaries\n");
    outb(0xf4, 0);
    for (;;) __asm__ volatile ("hlt");
}

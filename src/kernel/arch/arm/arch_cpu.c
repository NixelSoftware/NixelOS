#include "types.h"
#include "arch_context.h"

volatile uint32_t arch_user_exit_requested;
uint32_t arch_user_kernel_esp;

static uint32_t kernel_stack_top;

void pc_speaker_tick(void) { }

uint32_t arch_irq_save(void) {
    uint32_t cpsr;
    __asm__ volatile ("mrs %0, cpsr" : "=r"(cpsr));
    __asm__ volatile ("cpsid i" : : : "memory");
    return cpsr;
}

void arch_irq_restore(uint32_t state) {
    __asm__ volatile ("msr cpsr_c, %0" : : "r"(state) : "memory");
}

void arch_cpu_relax(void) {
    __asm__ volatile ("yield" : : : "memory");
}

void arch_init(void) { }
void arch_trap_init(void) { }

__attribute__((noreturn)) void arch_halt_forever(void) {
    __asm__ volatile ("cpsid i" : : : "memory");
    for (;;) __asm__ volatile ("wfi");
}

int arch_irq_were_enabled(uint32_t state) {
    return !(state & 0x80u);
}

void arch_irq_enable(void) {
    __asm__ volatile ("cpsie i" : : : "memory");
}

void arch_irq_disable(void) {
    __asm__ volatile ("cpsid i" : : : "memory");
}

void arch_halt(void) {
    __asm__ volatile ("cpsie i" : : : "memory");
    __asm__ volatile ("wfi" : : : "memory");
}

void arch_set_kernel_stack(uint32_t stack_top) {
    kernel_stack_top = stack_top;
}

uint32_t arch_fpu_init(arch_fpu_state_t* state) {
    (void)state;
    return 1;
}

uint32_t arch_fpu_features(void) {
    return 0;
}

void arch_fpu_save(arch_fpu_state_t* state) {
    (void)state;
}

void arch_fpu_restore(const arch_fpu_state_t* state) {
    (void)state;
}

extern void _task_start(void);

void arch_task_init(struct arch_context* ctx, void (*entry)(void*), void* arg,
                    uint32_t stack_top) {
    uint32_t* sp = (uint32_t*)(stack_top & ~7u) - 16;
    for (int i = 0; i < 16; i++)
        sp[i] = 0;
    sp[4] = (uint32_t)arg;
    sp[5] = (uint32_t)entry;
    sp[14] = (uint32_t)_task_start;
    sp[15] = 0x13u;
    ctx->esp = (uint32_t)sp;
}

#ifndef KERNEL_ARCH_ARM_ARCH_CONTEXT_H
#define KERNEL_ARCH_ARM_ARCH_CONTEXT_H

#include "../../types.h"

typedef struct { uint32_t unused; } arch_fpu_state_t;

uint32_t arch_fpu_init(arch_fpu_state_t* state);
uint32_t arch_fpu_features(void);
void arch_fpu_save(arch_fpu_state_t* state);
void arch_fpu_restore(const arch_fpu_state_t* state);

struct arch_context {
    uint32_t esp;
    uint32_t user_kernel_esp;
    uint32_t user_exit_requested;
    arch_fpu_state_t fpu_state;
};

#endif

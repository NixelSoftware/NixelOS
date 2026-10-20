#ifndef KERNEL_ARCH_I386_H
#define KERNEL_ARCH_I386_H

#include "types.h"

#define USER_CODE_SELECTOR 0x1Bu
#define USER_DATA_SELECTOR 0x23u

void arch_i386_init(void);
void arch_i386_set_kernel_stack(uint32_t stack_top);

#endif

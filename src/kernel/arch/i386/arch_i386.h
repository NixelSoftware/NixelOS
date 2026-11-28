#ifndef KERNEL_ARCH_I386_ARCH_I386_H
#define KERNEL_ARCH_I386_ARCH_I386_H

#include "../arch.h"

#define USER_CODE_SELECTOR 0x1Bu
#define USER_DATA_SELECTOR 0x23u

__attribute__((noreturn)) void arch_boot_entry(void);

#endif

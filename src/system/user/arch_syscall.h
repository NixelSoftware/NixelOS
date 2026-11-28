#ifndef SYSTEM_USER_ARCH_SYSCALL_H
#define SYSTEM_USER_ARCH_SYSCALL_H

#include "../../kernel/types.h"

#if defined(__i386__)
static inline uint32_t arch_syscall(uint32_t nr, uint32_t a0, uint32_t a1,
                                    uint32_t a2, uint32_t a3) {
    uint32_t result;
    __asm__ volatile ("int $0x80"
        : "=a"(result)
        : "0"(nr), "b"(a0), "c"(a1), "d"(a2), "S"(a3)
        : "memory", "cc");
    return result;
}
#elif !defined(NIXEL_KERNEL)
#error "arch_syscall: no syscall ABI for this architecture"
#endif

#endif

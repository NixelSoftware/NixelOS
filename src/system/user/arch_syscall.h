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
#elif defined(__arm__)
static inline uint32_t arch_syscall(uint32_t nr, uint32_t a0, uint32_t a1,
                                    uint32_t a2, uint32_t a3) {
    (void)a3;
    register uint32_t r0 __asm__("r0") = a0;
    register uint32_t r1 __asm__("r1") = a1;
    register uint32_t r2 __asm__("r2") = a2;
    register uint32_t r7 __asm__("r7") = nr;
    __asm__ volatile ("svc #0"
        : "+r"(r0)
        : "r"(r1), "r"(r2), "r"(r7)
        : "memory");
    return r0;
}
#elif !defined(NIXEL_KERNEL)
#error "arch_syscall: no syscall ABI for this architecture"
#endif

#endif

#ifndef SYSTEM_USER_SYS_API_H
#define SYSTEM_USER_SYS_API_H

#include "../../kernel/types.h"

/* Initial i386 system-call ABI. EAX selects the call; results return in EAX. */
enum {
    SYS_WRITE = 1,     /* EBX: byte buffer, ECX: byte count; returns bytes or -1 */
    SYS_GET_TICKS = 2  /* returns the kernel tick counter */
};

static inline int32_t sys_write(const char* buffer, uint32_t length) {
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_WRITE), "b"(buffer), "c"(length)
                      : "edx", "memory", "cc");
    return result;
}

static inline uint32_t sys_get_ticks(void) {
    uint32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "0"((uint32_t)SYS_GET_TICKS)
                      : "ebx", "ecx", "edx", "memory", "cc");
    return result;
}

#endif

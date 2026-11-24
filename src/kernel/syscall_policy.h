#ifndef KERNEL_SYSCALL_POLICY_H
#define KERNEL_SYSCALL_POLICY_H
#include "users.h"
#include "../system/user/sys_api.h"

/* Single authorization point for machine-wide services. Read-only snapshots,
 * owner-scoped windows/audio/sockets and permission-checked filesystem calls
 * remain available to ordinary users. UID 1 (system) is not a root alias. */
static inline int syscall_allowed(uint16_t uid, uint32_t call, uint32_t argument) {
    switch (call) {
        case SYS_SHUTDOWN:
        case SYS_REBOOT:
        case SYS_FLOPPY_INIT:
        case SYS_FLOPPY_READ:
        case SYS_FLOPPY_TEST_WRITE:
        case SYS_SERIAL_LOG_READ:
            return uid == USER_ROOT;
        case SYS_TCP_LISTEN:
            return uid == USER_ROOT || argument == 0 || argument >= 1024;
        default:
            return 1;
    }
}
#endif

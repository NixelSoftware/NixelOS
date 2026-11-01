#ifndef NIXEL2LINUX_ABI_H
#define NIXEL2LINUX_ABI_H

#include "../types.h"

/* Linux i386 Syscall Numbers */
#define LINUX_SYS_EXIT           1
#define LINUX_SYS_FORK           2
#define LINUX_SYS_READ           3
#define LINUX_SYS_WRITE          4
#define LINUX_SYS_OPEN           5
#define LINUX_SYS_CLOSE          6
#define LINUX_SYS_UNLINK        10
#define LINUX_SYS_EXECVE        11
#define LINUX_SYS_CHDIR         12
#define LINUX_SYS_TIME          13
#define LINUX_SYS_LSEEK         19
#define LINUX_SYS_GETPID        20
#define LINUX_SYS_ACCESS        33
#define LINUX_SYS_BRK           45
#define LINUX_SYS_IOCTL         54
#define LINUX_SYS_GETTIMEOFDAY  78
#define LINUX_SYS_MMAP          90
#define LINUX_SYS_MUNMAP        91
#define LINUX_SYS_STAT         106
#define LINUX_SYS_FSTAT        108
#define LINUX_SYS_UNAME        122
#define LINUX_SYS_LLSEEK       140
#define LINUX_SYS_WRITEV       146
#define LINUX_SYS_MMAP2        192
#define LINUX_SYS_EXIT_GROUP   252

/* Linux Error Codes (Returned as negative values) */
#define LINUX_EPERM       1
#define LINUX_ENOENT      2
#define LINUX_ESRCH       3
#define LINUX_EINTR       4
#define LINUX_EIO         5
#define LINUX_EBADF       9
#define LINUX_ENOMEM     12
#define LINUX_EACCES     13
#define LINUX_EFAULT     14
#define LINUX_EINVAL     22
#define LINUX_ENFILE     23
#define LINUX_EMFILE     24
#define LINUX_ENOSYS     38

/* Linux utsname structure for sys_uname */
struct linux_old_utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

/* Linux iovec for sys_writev */
struct linux_iovec {
    void*  iov_base;
    uint32_t iov_len;
};

#endif
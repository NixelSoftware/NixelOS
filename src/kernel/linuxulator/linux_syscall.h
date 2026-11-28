#ifndef NIXEL2LINUX_SYSCALL_H
#define NIXEL2LINUX_SYSCALL_H

#include "../arch/arch.h"

void linux_syscall_dispatch(arch_syscall_t* call);

#endif
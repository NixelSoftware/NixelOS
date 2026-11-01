#ifndef NIXEL2LINUX_SYSCALL_H
#define NIXEL2LINUX_SYSCALL_H

#include "../idt.h"

void linux_syscall_dispatch(registers_t* regs);

#endif
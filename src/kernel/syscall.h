#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include "types.h"
#include "idt.h"
#include "../system/user/sys_api.h"

void syscall_dispatch(registers_t* regs);

#endif

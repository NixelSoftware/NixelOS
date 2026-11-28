#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include "types.h"
#include "arch/arch.h"
#include "../system/user/sys_api.h"

void syscall_dispatch(arch_syscall_t* call);
void syscall_forget_address_space(uint32_t directory_address);

#endif

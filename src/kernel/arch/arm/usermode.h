#ifndef ARCH_ARM_USERMODE_H
#define ARCH_ARM_USERMODE_H

#include "types.h"

void usermode_task(void* arg);
uint32_t arm_syscall(uint32_t number, uint32_t a0, uint32_t a1, uint32_t a2);

#endif

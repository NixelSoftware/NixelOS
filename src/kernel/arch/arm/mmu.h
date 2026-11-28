#ifndef ARCH_ARM_MMU_H
#define ARCH_ARM_MMU_H

#include "types.h"

void arm_mmu_init(void);
uint32_t arm_mmu_sctlr(void);
uint32_t* arm_space_create_user(void);

#endif

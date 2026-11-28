#ifndef ARCH_ARM_GIC_H
#define ARCH_ARM_GIC_H

#include "types.h"

void arm_gic_init(void);
uint32_t arm_gic_ack(void);
void arm_gic_eoi(uint32_t iar);

#endif

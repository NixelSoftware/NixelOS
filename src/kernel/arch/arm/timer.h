#ifndef ARCH_ARM_TIMER_H
#define ARCH_ARM_TIMER_H

#include "types.h"

uint32_t arm_timer_init(uint32_t hz);
void arm_timer_rearm(void);
uint32_t arm_timer_frequency(void);

#endif

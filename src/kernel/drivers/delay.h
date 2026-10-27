#ifndef NIXEL_DRIVER_DELAY_H
#define NIXEL_DRIVER_DELAY_H
#include "../types.h"
/* Calibrated x86 device delays, also usable before scheduler/timer IRQs. */
void driver_delay_init(void);
void driver_delay_us(uint32_t microseconds);
#endif

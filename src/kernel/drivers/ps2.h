#ifndef KERNEL_DRIVERS_PS2_H
#define KERNEL_DRIVERS_PS2_H

#include "../types.h"

uint8_t ps2_controller_status(void);
uint8_t ps2_controller_read_data(void);
void ps2_mouse_initialize(void);

#endif

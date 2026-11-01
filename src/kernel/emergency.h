#ifndef KERNEL_EMERGENCY_H
#define KERNEL_EMERGENCY_H

#include "types.h"

void emergency_console_clear(void);
void emergency_console_print(const char* text, uint32_t color);

/* Clears all GUI rendering, prints error text, and halts the CPU */
__attribute__((noreturn)) void emergency_panic(const char* title, const char* message);

#endif
#ifndef SERIAL_H
#define SERIAL_H

#include "../types.h"

void serial_init(void);
void debug_putc(char c);
void debug_print(const char* str);
void debug_hex(uint32_t val);

/* Set to 1 (or compile with -DDEBUG_SUBSYSTEMS=1) for verbose subsystem traces. */
#ifndef DEBUG_SUBSYSTEMS
#define DEBUG_SUBSYSTEMS 0
#endif

#if DEBUG_SUBSYSTEMS
#define debug_subsystem_print(...) debug_print(__VA_ARGS__)
#define debug_subsystem_hex(...) debug_hex(__VA_ARGS__)
#else
#define debug_subsystem_print(...) ((void)0)
#define debug_subsystem_hex(...) ((void)0)
#endif

#endif

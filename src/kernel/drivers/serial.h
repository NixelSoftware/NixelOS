#ifndef SERIAL_H
#define SERIAL_H

#include "../types.h"

void serial_init(void);
void debug_putc(char c);
void debug_print(const char* str);
void debug_hex(uint32_t val);

#endif
#ifndef BOARD_VIRT_SERIAL_H
#define BOARD_VIRT_SERIAL_H

#include "types.h"

void virt_serial_init(void);
void virt_serial_putc(char c);
void virt_serial_write(const char* text);

#endif

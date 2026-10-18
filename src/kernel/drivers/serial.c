#include "serial.h"

#define COM1_PORT 0x3F8

void serial_init(void) {
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x03); // 38400 baud
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);
}

static int is_transmit_empty(void) {
    return inb(COM1_PORT + 5) & 0x20;
}

void debug_putc(char c) {
    while (!is_transmit_empty()) {
        __asm__ volatile ("pause");
    }
    outb(COM1_PORT, c);
}

void debug_print(const char* str) {
    while (*str) {
        if (*str == '\n') {
            debug_putc('\r');
        }
        debug_putc(*str);
        str++;
    }
}

void debug_hex(uint32_t val) {
    const char hex_chars[] = "0123456789ABCDEF";
    debug_print("0x");
    for (int i = 7; i >= 0; i--) {
        debug_putc(hex_chars[(val >> (i * 4)) & 0x0F]);
    }
}
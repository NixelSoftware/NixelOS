#include "serial.h"

#define UART0_BASE 0x09000000u

#define UART_DR    0x00u
#define UART_FR    0x18u
#define UART_IBRD  0x24u
#define UART_FBRD  0x28u
#define UART_LCRH  0x2Cu
#define UART_CR    0x30u
#define UART_IMSC  0x38u
#define UART_ICR   0x44u

static inline void wr(uint32_t reg, uint32_t value) {
    *(volatile uint32_t*)(UART0_BASE + reg) = value;
}

static inline uint32_t rd(uint32_t reg) {
    return *(volatile uint32_t*)(UART0_BASE + reg);
}

void virt_serial_init(void) {
    wr(UART_CR, 0);
    wr(UART_ICR, 0x7FFu);
    wr(UART_IMSC, 0);
    wr(UART_IBRD, 13);
    wr(UART_FBRD, 1);
    wr(UART_LCRH, 0x70u);
    wr(UART_CR, 0x301u);
}

void virt_serial_putc(char c) {
    while (rd(UART_FR) & (1u << 5)) { }
    wr(UART_DR, (uint32_t)(uint8_t)c);
}

void virt_serial_write(const char* text) {
    while (*text) virt_serial_putc(*text++);
}

#ifndef KERNEL_IRQ_H
#define KERNEL_IRQ_H
#include "types.h"

/* Legacy PIC lines for PCI devices. A handler runs with IRQs off: it must not
 * lock or sleep, and returns nonzero only after quieting its own device. */
typedef int (*irq_handler_t)(void);
/* Fails (the caller keeps polling) for reserved or already claimed lines. */
int irq_register(uint8_t line, irq_handler_t handler);
void irq_unregister(uint8_t line);
/* False once unregistered, or masked because nobody claimed its interrupts. */
int irq_active(uint8_t line);
/* Returns nonzero if the vector belonged to a registered line. */
int irq_dispatch(uint32_t vector);
#endif

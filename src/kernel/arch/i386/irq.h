#ifndef KERNEL_ARCH_I386_IRQ_H
#define KERNEL_ARCH_I386_IRQ_H

#include "../../types.h"
#include "../arch.h"

int irq_dispatch(uint32_t vector);

#endif

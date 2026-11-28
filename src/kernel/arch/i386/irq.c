#include "irq.h"
#include "../../task.h"

/* PCI lines are shared and level triggered. A device without a handler would
 * otherwise re-enter forever, so give up on the line and let owners poll. */
#define IRQ_STORM_LIMIT 1000u
#define IRQ_LINES       16u

static arch_irq_handler_t handlers[IRQ_LINES];
static void* handler_ctx[IRQ_LINES];
static uint16_t unclaimed[IRQ_LINES];

/* 0 timer, 2 cascade, 5 SB16, 7/15 spurious, 14 IDE; the rest are ISA. */
static int usable(uint32_t line) {
    return line == 3 || line == 4 || (line >= 9 && line <= 11);
}
static void set_masked(uint32_t line, int masked) {
    uint16_t port = line < 8 ? 0x21 : 0xA1;
    uint8_t bit = (uint8_t)(1u << (line & 7));
    uint8_t mask = inb(port);
    outb(port, masked ? mask | bit : mask & ~bit);
    if (!masked && line >= 8) outb(0x21, inb(0x21) & ~4u); /* cascade */
}
int arch_irq_register(uint32_t line, arch_irq_handler_t handler, void* ctx) {
    if (!handler || line >= IRQ_LINES || !usable(line)) return 0;
    uint32_t flags = task_irq_save();
    int ok = !handlers[line];
    if (ok) {
        handlers[line] = handler;
        handler_ctx[line] = ctx;
        unclaimed[line] = 0;
        set_masked(line, 0);
    }
    task_irq_restore(flags);
    return ok;
}
void arch_irq_unregister(uint32_t line) {
    if (line >= IRQ_LINES || !usable(line)) return;
    uint32_t flags = task_irq_save();
    if (handlers[line]) {
        set_masked(line, 1);
        handlers[line] = 0;
        handler_ctx[line] = 0;
    }
    task_irq_restore(flags);
}
int arch_irq_active(uint32_t line) {
    return line < IRQ_LINES && usable(line) && handlers[line] &&
           unclaimed[line] < IRQ_STORM_LIMIT;
}
void arch_irq_eoi(uint32_t line) {
    /* EOI only after the device has dropped its line. */
    if (line >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}
int irq_dispatch(uint32_t vector) {
    if (vector < 32 || vector >= 32 + IRQ_LINES) return 0;
    uint32_t line = vector - 32;
    if (!usable(line)) return 0;
    if (handlers[line] && handlers[line](handler_ctx[line])) unclaimed[line] = 0;
    else if (!handlers[line] || ++unclaimed[line] >= IRQ_STORM_LIMIT)
        set_masked(line, 1);
    arch_irq_eoi(line);
    return 1;
}

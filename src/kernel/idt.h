#ifndef IDT_H
#define IDT_H

#include "types.h"

typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
} registers_t;
/* Vector/error slots distinguish this ABI from the timer/yield frame.
 * useresp/ss exist on the CPU stack only when CS indicates a user return. */
_Static_assert(__builtin_offsetof(registers_t, int_no) == 48, "vector offset");
_Static_assert(__builtin_offsetof(registers_t, eip) == 56, "exception IRET offset");
_Static_assert(__builtin_offsetof(registers_t, cs) == 60, "abort CS offset");
_Static_assert(__builtin_offsetof(registers_t, useresp) == 68, "exception user ESP offset");
_Static_assert(sizeof(registers_t) == 76, "user exception frame size");

struct idt_entry_struct {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));
typedef struct idt_entry_struct idt_entry_t;

struct idt_ptr_struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));
typedef struct idt_ptr_struct idt_ptr_t;

void idt_init(void);
void isr_handler(registers_t* regs);

#endif
#include "arch_i386.h"

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_pointer_t;

typedef struct {
    uint32_t previous, esp0;
    uint32_t ss0, esp1, ss1, esp2, ss2, cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs, ldt;
    uint16_t trap, iomap_base;
} __attribute__((packed)) tss_t;
_Static_assert(sizeof(tss_t) == 104, "i386 TSS size");
_Static_assert(__builtin_offsetof(tss_t, esp0) == 4, "TSS ESP0 offset");
_Static_assert(__builtin_offsetof(tss_t, iomap_base) == 102, "TSS I/O map offset");

extern void gdt_load(const gdt_pointer_t* pointer);
extern void tss_load(void);

static gdt_entry_t gdt[6];
static gdt_pointer_t gdt_pointer;
static tss_t tss;
static uint8_t privilege_stack[16384] __attribute__((aligned(16)));

static void gdt_set(uint32_t index, uint32_t base, uint32_t limit,
                    uint8_t access, uint8_t granularity) {
    gdt[index].base_low = (uint16_t)(base & 0xFFFFu);
    gdt[index].base_middle = (uint8_t)((base >> 16) & 0xFFu);
    gdt[index].base_high = (uint8_t)((base >> 24) & 0xFFu);
    gdt[index].limit_low = (uint16_t)(limit & 0xFFFFu);
    gdt[index].granularity = (uint8_t)(((limit >> 16) & 0x0Fu) | (granularity & 0xF0u));
    gdt[index].access = access;
}

void arch_i386_set_kernel_stack(uint32_t stack_top) {
    tss.esp0 = stack_top;
}

void arch_i386_init(void) {
    gdt_pointer.limit = sizeof(gdt) - 1u;
    gdt_pointer.base = (uint32_t)gdt;
    gdt_set(0, 0, 0, 0, 0);
    gdt_set(1, 0, 0xFFFFFu, 0x9Au, 0xCFu); /* kernel code */
    gdt_set(2, 0, 0xFFFFFu, 0x92u, 0xCFu); /* kernel data */
    gdt_set(3, 0, 0xFFFFFu, 0xFAu, 0xCFu); /* user code */
    gdt_set(4, 0, 0xFFFFFu, 0xF2u, 0xCFu); /* user data */

    tss.ss0 = 0x10u;
    tss.esp0 = (uint32_t)(privilege_stack + sizeof(privilege_stack));
    tss.iomap_base = sizeof(tss);
    gdt_set(5, (uint32_t)&tss, sizeof(tss) - 1u, 0x89u, 0x00u);
    gdt_load(&gdt_pointer);
    tss_load();
}

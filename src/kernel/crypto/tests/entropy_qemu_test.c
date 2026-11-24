/* Ring-0 integration on an emulated Pentium III (TSC, no RDRAND). */
#include "../../types.h"
#define KERNEL_MUTEX_H
#define MUTEX_GUARD(lock) ((void)(&(lock)))
typedef struct { unsigned depth; } mutex_t;
static mutex_t kernel_mutex;
static uint32_t task_irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) :: "memory");
    return flags;
}
static void task_irq_restore(uint32_t flags) {
    __asm__ volatile("pushl %0; popfl" :: "r"(flags) : "memory", "cc");
}
#include "../../paging.c"
#include "../entropy.c"
static void finish(unsigned value) {
    __asm__ volatile("outl %0, %1" :: "a"(value), "Nd"((uint16_t)0xf4));
    for (;;) __asm__ volatile("hlt");
}
void test_main(void) {
    bios_memory_entry_t* e = (bios_memory_entry_t*)0x5004;
    e[0] = (bios_memory_entry_t){0x100000,63u*1024*1024,1,1};
    *(volatile uint32_t*)0x5000 = 1;
    paging_init();
    if (entropy_cpu_features() != 1) finish(0x11);
    uint8_t first[32], second[32];
    if (kernel_entropy(0,first,32) || kernel_entropy(0,second,32)) finish(0x12);
    unsigned different = 0, nonzero = 0;
    for (unsigned i = 0; i < 32; i++) {
        different |= first[i] ^ second[i]; nonzero |= first[i];
    }
    if (!different || !nonzero) finish(0x13);
    finish(0x10);
}

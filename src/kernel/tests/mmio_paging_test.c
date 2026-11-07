/* Runs at ring 0 in QEMU: exercise the real cache flags and CR3 updates. */
#include "../types.h"
#define KERNEL_MUTEX_H
#define MUTEX_GUARD(lock) ((void)0)
static uint32_t task_irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) :: "memory");
    return flags;
}
static void task_irq_restore(uint32_t flags) {
    __asm__ volatile("pushl %0; popfl" :: "r"(flags) : "memory", "cc");
}
#include "../paging.c"
static void check(int ok) {
    if (ok) return;
    __asm__ volatile("outl %0, %1" :: "a"(0x11), "Nd"((uint16_t)0xf4));
    for (;;) __asm__ volatile("hlt");
}
void test_main(void) {
    bios_memory_entry_t* e = (bios_memory_entry_t*)0x5004;
    e[0] = (bios_memory_entry_t){0x100000,63u*1024*1024,1,1};
    e[1] = (bios_memory_entry_t){0x90000000u,PAGE_SIZE,1,1};
    *(volatile uint32_t*)0x5000 = 2;
    paging_init();
    address_space_t old_app, new_app;
    check(!paging_address_space_create(&old_app));
    paging_activate(&old_app);
    uint32_t mmio = 0xf0001000u, d = mmio >> 22;
    uint32_t old_entry = old_app.directory[d];
    check(old_entry & PAGE_SIZE_4M);
    check(!paging_map_mmio_page(0x1000000)); /* RAM must stay cached. */
    check(!paging_map_mmio_page(APP_HEAP_BASE));
    check(!paging_map_mmio_page(0x90000000u)); /* High RAM, outside heap. */
    check(!paging_map_mmio_page(mmio + 1));
    check(paging_map_mmio_page(mmio));
    check(old_app.directory[d] == page_directory[d]);
    check(!(page_directory[d] & PAGE_SIZE_4M));
    uint32_t* table = (uint32_t*)(page_directory[d] & PAGE_ADDRESS_MASK);
    check(table[1] == (mmio | PAGE_PRESENT | PAGE_WRITABLE | PAGE_UNCACHED));
    check(table[0] == (0xf0000000u | PAGE_PRESENT | PAGE_WRITABLE));
    check(!(table[1] & PAGE_USER));
    check(paging_map_mmio_page(mmio)); /* Idempotent, no leaked split table. */
    check(paging_map_mmio_page(mmio + PAGE_SIZE));
    check((table[2] & PAGE_UNCACHED) == PAGE_UNCACHED);
    check(!paging_address_space_create(&new_app));
    check(new_app.directory[d] == page_directory[d]);
    /* A directory with the original large mapping is synchronized on entry. */
    old_app.directory[d] = old_entry;
    paging_activate_kernel();
    paging_activate(&old_app);
    check(old_app.directory[d] == page_directory[d]);
    check(paging_current_directory() == (uint32_t)old_app.directory);
    paging_activate_kernel();
    paging_address_space_destroy(&old_app);
    paging_address_space_destroy(&new_app);
    check(table[1] & PAGE_UNCACHED); /* App destruction preserves kernel table. */
    __asm__ volatile("outl %0, %1" :: "a"(0x10), "Nd"((uint16_t)0xf4));
    for (;;) __asm__ volatile("hlt");
}

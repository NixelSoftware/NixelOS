#include "types.h"
#include "mm.h"
#include "arch/arch.h"

#define RAM_BASE 0x40000000u
#define HEAP_END 0x4F000000u

extern uint8_t __bss_end[];

static uint32_t heap_next;
static uint32_t heap_end;

void paging_init(const arch_boot_info_t* boot) {
    (void)boot;
    heap_next = ((uint32_t)__bss_end + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
    heap_end = HEAP_END;
}

uint8_t paging_is_enabled(void) {
    return 1;
}

int paging_map_mmio_page(uint32_t physical) {
    (void)physical;
    return 0;
}

uint32_t paging_alloc_contiguous(uint32_t pages) {
    if (!pages) return 0;
    uint64_t size = (uint64_t)pages * PAGE_SIZE;
    if (heap_next + size > heap_end) return 0;
    uint32_t address = heap_next;
    heap_next += (uint32_t)size;
    for (uint32_t i = 0; i < pages; i++) {
        uint32_t* frame = (uint32_t*)(address + i * PAGE_SIZE);
        for (uint32_t word = 0; word < PAGE_SIZE / 4u; word++) frame[word] = 0;
    }
    return address;
}

uint32_t paging_frame_alloc(void) {
    return paging_alloc_contiguous(1);
}

void paging_frame_free(uint32_t physical_address) {
    (void)physical_address;
}

void paging_memory_info(user_memory_info_t* out) {
    if (!out) return;
    uint32_t used = heap_next > RAM_BASE ? heap_next - RAM_BASE : 0;
    uint32_t available = heap_end > heap_next ? heap_end - heap_next : 0;
    out->total_bytes = used + available;
    out->used_bytes = used;
    out->available_bytes = available;
    out->reserved_bytes = 0;
}

uint32_t paging_user_alloc(uint32_t bytes) {
    if (!bytes) return 0;
    return paging_alloc_contiguous((bytes + PAGE_SIZE - 1u) / PAGE_SIZE);
}

int paging_user_free(uint32_t address) {
    (void)address;
    return 0;
}

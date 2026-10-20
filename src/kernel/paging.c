#include "paging.h"

/*
 * This kernel currently boots with 32 MiB configured in QEMU. The boot stack
 * is at 15 MiB and the flat kernel and legacy app loader use the first 4 MiB,
 * so the page-frame pool starts at 16 MiB. Add a BIOS memory map before using
 * this allocator on machines with a different memory layout.
 */
#define PAGE_DIRECTORY_ENTRIES 1024u
#define PAGE_TABLE_ENTRIES     1024u
#define FRAME_POOL_BASE        0x01000000u
#define FRAME_POOL_LIMIT       0x02000000u
#define FRAME_COUNT            ((FRAME_POOL_LIMIT - FRAME_POOL_BASE) / PAGE_SIZE)
#define FRAME_BITMAP_BYTES     (FRAME_COUNT / 8u)

#define PAGE_PRESENT           0x001u
#define PAGE_WRITABLE          0x002u
#define PAGE_USER              0x004u
#define PAGE_SIZE_4M           0x080u
#define PAGE_ADDRESS_MASK      0xFFFFF000u
#define CR0_PG                 0x80000000u
#define CR4_PSE                0x00000010u

static uint32_t page_directory[PAGE_DIRECTORY_ENTRIES]
    __attribute__((aligned(PAGE_SIZE)));
static uint32_t low_page_table[PAGE_TABLE_ENTRIES]
    __attribute__((aligned(PAGE_SIZE)));
static uint8_t frame_bitmap[FRAME_BITMAP_BYTES];
static uint32_t active_directory;
static uint32_t next_frame;

static uint8_t frame_is_used(uint32_t index) {
    return (frame_bitmap[index >> 3] & (1u << (index & 7u))) != 0;
}

static void frame_set_used(uint32_t index, uint8_t used) {
    uint8_t mask = (uint8_t)(1u << (index & 7u));
    if (used) frame_bitmap[index >> 3] |= mask;
    else frame_bitmap[index >> 3] &= (uint8_t)~mask;
}

static uint32_t* address_space_low_table(address_space_t* space) {
    if (!space || !space->directory || !space->app_page_table) return 0;
    return space->app_page_table;
}

void paging_init(void) {
    for (uint32_t i = 0; i < PAGE_TABLE_ENTRIES; i++)
        low_page_table[i] = (i * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITABLE;

    page_directory[0] = ((uint32_t)low_page_table) | PAGE_PRESENT | PAGE_WRITABLE;
    for (uint32_t i = 1; i < PAGE_DIRECTORY_ENTRIES; i++)
        page_directory[i] = (i << 22) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_SIZE_4M;

    for (uint32_t i = 0; i < FRAME_BITMAP_BYTES; i++) frame_bitmap[i] = 0;
    next_frame = 0;
    active_directory = (uint32_t)page_directory;

    uint32_t cr4;
    uint32_t cr0;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= CR4_PSE;
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4) : "memory");
    __asm__ volatile ("mov %0, %%cr3" : : "r"(active_directory) : "memory");
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= CR0_PG;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

uint8_t paging_is_enabled(void) {
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    return (cr0 & CR0_PG) != 0;
}

uint32_t paging_frame_alloc(void) {
    for (uint32_t scanned = 0; scanned < FRAME_COUNT; scanned++) {
        uint32_t index = (next_frame + scanned) % FRAME_COUNT;
        if (frame_is_used(index)) continue;
        frame_set_used(index, 1);
        next_frame = (index + 1) % FRAME_COUNT;
        uint32_t address = FRAME_POOL_BASE + index * PAGE_SIZE;
        uint32_t* words = (uint32_t*)address;
        for (uint32_t word = 0; word < PAGE_SIZE / sizeof(uint32_t); word++)
            words[word] = 0;
        return address;
    }
    return 0;
}

void paging_frame_free(uint32_t physical_address) {
    if (physical_address < FRAME_POOL_BASE || physical_address >= FRAME_POOL_LIMIT ||
        (physical_address & (PAGE_SIZE - 1u)) != 0) return;

    uint32_t index = (physical_address - FRAME_POOL_BASE) / PAGE_SIZE;
    if (!frame_is_used(index)) return;
    frame_set_used(index, 0);
    if (index < next_frame) next_frame = index;
}

int paging_address_space_create(address_space_t* space) {
    if (!space) return -1;
    space->directory = 0;
    space->app_page_table = 0;

    uint32_t directory_frame = paging_frame_alloc();
    if (!directory_frame) return -1;
    uint32_t table_frame = paging_frame_alloc();
    if (!table_frame) {
        paging_frame_free(directory_frame);
        return -1;
    }

    space->directory = (uint32_t*)directory_frame;
    space->app_page_table = (uint32_t*)table_frame;
    uint32_t* kernel_directory = page_directory;
    for (uint32_t i = 0; i < PAGE_DIRECTORY_ENTRIES; i++)
        space->directory[i] = kernel_directory[i];
    for (uint32_t i = 0; i < PAGE_TABLE_ENTRIES; i++)
        space->app_page_table[i] = low_page_table[i];

    /* Keep the kernel's first 4 MiB map intact; apps use a private 4-6 MiB slot. */
    uint32_t app_directory_index = APP_VIRTUAL_BASE >> 22;
    uint32_t app_page_count = (APP_VIRTUAL_LIMIT - APP_VIRTUAL_BASE) / PAGE_SIZE;
    for (uint32_t i = 0; i < PAGE_TABLE_ENTRIES; i++) {
        uint32_t physical_address = (app_directory_index << 22) + i * PAGE_SIZE;
        space->app_page_table[i] = physical_address | PAGE_PRESENT | PAGE_WRITABLE;
    }
    for (uint32_t i = 0; i < app_page_count; i++)
        space->app_page_table[i] = 0;
    space->directory[app_directory_index] = table_frame | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    return 0;
}

int paging_address_space_alloc_page(address_space_t* space, uint32_t virtual_address,
                                    uint8_t writable, uint32_t* physical_out) {
    if (!space || !space->directory || !space->app_page_table ||
        virtual_address < APP_VIRTUAL_BASE || virtual_address >= APP_VIRTUAL_LIMIT ||
        (virtual_address & (PAGE_SIZE - 1u)) != 0) return -1;

    uint32_t page_index = (virtual_address >> 12) & (PAGE_TABLE_ENTRIES - 1u);
    uint32_t* table = address_space_low_table(space);
    if (table[page_index] & PAGE_PRESENT) return -1;

    uint32_t frame = paging_frame_alloc();
    if (!frame) return -1;
    table[page_index] = frame | PAGE_PRESENT | PAGE_USER | (writable ? PAGE_WRITABLE : 0);
    if (physical_out) *physical_out = frame;
    return 0;
}

void paging_address_space_destroy(address_space_t* space) {
    if (!space || !space->directory || !space->app_page_table) return;
    if (active_directory == (uint32_t)space->directory) paging_activate_kernel();

    uint32_t app_page_count = (APP_VIRTUAL_LIMIT - APP_VIRTUAL_BASE) / PAGE_SIZE;
    for (uint32_t i = 0; i < app_page_count; i++) {
        uint32_t entry = space->app_page_table[i];
        if (entry & PAGE_PRESENT) paging_frame_free(entry & PAGE_ADDRESS_MASK);
    }

    paging_frame_free((uint32_t)space->app_page_table);
    paging_frame_free((uint32_t)space->directory);
    space->app_page_table = 0;
    space->directory = 0;
}

void paging_activate(address_space_t* space) {
    if (!space || !space->directory) return;
    paging_activate_directory((uint32_t)space->directory);
}

void paging_activate_directory(uint32_t directory_address) {
    if (!directory_address || (directory_address & (PAGE_SIZE - 1u))) return;
    active_directory = directory_address;
    __asm__ volatile ("mov %0, %%cr3" : : "r"(active_directory) : "memory");
}

void paging_activate_kernel(void) {
    active_directory = (uint32_t)page_directory;
    __asm__ volatile ("mov %0, %%cr3" : : "r"(active_directory) : "memory");
}

uint32_t paging_current_directory(void) {
    return active_directory;
}

uint32_t paging_kernel_directory(void) {
    return (uint32_t)page_directory;
}

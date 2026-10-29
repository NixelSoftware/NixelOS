#include "mutex.h"
#include "paging.h"

/* The first 16 MiB contains fixed kernel buffers and the boot stack.
 * Allocate only complete pages advertised as usable by BIOS E820.
 * The final physical page is excluded to keep byte counts in the 32-bit ABI.
 */
#define PAGE_DIRECTORY_ENTRIES 1024u
#define PAGE_TABLE_ENTRIES     1024u
#define FRAME_POOL_BASE        0x01000000u
#define FRAME_POOL_LIMIT       0xFFFFF000u
#define FRAME_COUNT            ((FRAME_POOL_LIMIT - FRAME_POOL_BASE) / PAGE_SIZE)
#define FRAME_BITMAP_BYTES     ((FRAME_COUNT + 7u) / 8u)

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
static uint8_t usable_bitmap[FRAME_BITMAP_BYTES];
static uint32_t detected_total, pool_pages;
static uint32_t next_frame;

typedef struct __attribute__((packed)) {
    uint64_t base, length;
    uint32_t type, attributes;
} bios_memory_entry_t;

static uint8_t frame_is_usable(uint32_t index) {
    return (usable_bitmap[index >> 3] & (1u << (index & 7u))) != 0;
}

static void paging_detect_memory(const bios_memory_entry_t* entries, uint32_t count) {
    detected_total = pool_pages = 0;
    for (uint32_t i = 0; i < FRAME_BITMAP_BYTES; ++i) {
        usable_bitmap[i] = 0;
        frame_bitmap[i] = 0;
    }
    if (count > 128) return;
    /* Reserved entries win over usable entries, including overlapping ranges. */
    for (uint32_t pass = 0; pass < 2; ++pass) {
        for (uint32_t i = 0; i < count; ++i) {
            const bios_memory_entry_t* e = &entries[i];
            if (!(e->attributes & 1) || !e->length || e->base >= FRAME_POOL_LIMIT)
                continue;
            uint8_t usable = e->type == 1;
            if (usable != (pass == 0)) continue;
            uint64_t end = e->base + e->length;
            if (end < e->base || end > FRAME_POOL_LIMIT) end = FRAME_POOL_LIMIT;
            uint64_t begin = e->base;
            if (usable) {
                begin = (begin + PAGE_SIZE - 1) & ~((uint64_t)PAGE_SIZE - 1);
                end &= ~((uint64_t)PAGE_SIZE - 1);
            } else {
                begin &= ~((uint64_t)PAGE_SIZE - 1);
                end = (end + PAGE_SIZE - 1) & ~((uint64_t)PAGE_SIZE - 1);
            }
            for (uint64_t address = begin; address < end; address += PAGE_SIZE) {
                /* Low RAM is counted separately after resolving overlaps below. */
                if (address < FRAME_POOL_BASE) continue;
                uint32_t index = ((uint32_t)address - FRAME_POOL_BASE) / PAGE_SIZE;
                uint8_t mask = 1u << (index & 7u);
                if (usable) usable_bitmap[index >> 3] |= mask;
                else usable_bitmap[index >> 3] &= (uint8_t)~mask;
            }
        }
    }
    /* Resolve low-memory pages too, without making them allocatable. */
    for (uint32_t address = 0; address < FRAME_POOL_BASE; address += PAGE_SIZE) {
        uint8_t usable = 0, reserved = 0;
        for (uint32_t i = 0; i < count; ++i) {
            const bios_memory_entry_t* e = &entries[i];
            if (!(e->attributes & 1) || !e->length) continue;
            uint64_t end = e->base + e->length;
            if (end < e->base) end = (uint64_t)-1;
            if (e->type == 1 && e->base <= address && end >= address + PAGE_SIZE)
                usable = 1;
            if (e->type != 1 && e->base < address + PAGE_SIZE && end > address)
                reserved = 1;
        }
        if (usable && !reserved) detected_total += PAGE_SIZE;
    }
    for (uint32_t i = 0; i < FRAME_COUNT; ++i)
        if (frame_is_usable(i)) ++pool_pages;
    detected_total += pool_pages * PAGE_SIZE;
    next_frame = 0;
}

static uint32_t active_directory;

static uint8_t frame_is_used(uint32_t index) {
    return (frame_bitmap[index >> 3] & (1u << (index & 7u))) != 0;
}

void paging_memory_info(user_memory_info_t* out) {
    MUTEX_GUARD(kernel_mutex);
    uint32_t allocated = 0;
    for (uint32_t i = 0; i < FRAME_COUNT; ++i)
        if (frame_is_usable(i) && frame_is_used(i)) ++allocated;
    if (!out) return;
    out->total_bytes = detected_total;
    out->reserved_bytes = detected_total - pool_pages * PAGE_SIZE;
    out->available_bytes = (pool_pages - allocated) * PAGE_SIZE;
    out->used_bytes = out->total_bytes - out->available_bytes;
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

    paging_detect_memory((const bios_memory_entry_t*)0x5004,
                         *(volatile uint32_t*)0x5000);
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
    MUTEX_GUARD(kernel_mutex);
    for (uint32_t scanned = 0; scanned < FRAME_COUNT; scanned++) {
        uint32_t index = (next_frame + scanned) % FRAME_COUNT;
        if (!frame_is_usable(index) || frame_is_used(index)) continue;
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

uint32_t paging_alloc_contiguous(uint32_t pages) {
    MUTEX_GUARD(kernel_mutex);
    if (!pages || pages > pool_pages) return 0;
    uint32_t run = 0;
    for (uint32_t i = 0; i < FRAME_COUNT; i++) {
        if (!frame_is_usable(i) || frame_is_used(i)) { run = 0; continue; }
        if (++run != pages) continue;
        uint32_t first = i + 1 - pages;
        for (uint32_t j = first; j <= i; j++) frame_set_used(j, 1);
        next_frame = (i + 1) % FRAME_COUNT;
        uint32_t address = FRAME_POOL_BASE + first * PAGE_SIZE;
        uint32_t* dest = (uint32_t*)address;
        uint32_t count = pages * (PAGE_SIZE / 4u);
        __asm__ volatile ("cld; rep stosl" : "+D"(dest), "+c"(count) : "a"(0) : "memory");
        return address;
    }
    return 0;
}

void paging_frame_free(uint32_t physical_address) {
    MUTEX_GUARD(kernel_mutex);
    if (physical_address < FRAME_POOL_BASE || physical_address >= FRAME_POOL_LIMIT ||
        (physical_address & (PAGE_SIZE - 1u)) != 0) return;

    uint32_t index = (physical_address - FRAME_POOL_BASE) / PAGE_SIZE;
    if (!frame_is_usable(index) || !frame_is_used(index)) return;
    frame_set_used(index, 0);
    if (index < next_frame) next_frame = index;
}

int paging_address_space_create(address_space_t* space) {
    MUTEX_GUARD(kernel_mutex);
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

    /* Apps use a private 12-14 MiB slot, outside kernel global data. */
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
    MUTEX_GUARD(kernel_mutex);
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
    MUTEX_GUARD(kernel_mutex);
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
    uint32_t flags = task_irq_save();
    active_directory = directory_address;
    __asm__ volatile ("mov %0, %%cr3" : : "r"(directory_address) : "memory");
    task_irq_restore(flags);
}

void paging_activate_kernel(void) {
    paging_activate_directory((uint32_t)page_directory);
}

uint32_t paging_current_directory(void) {
    return active_directory;
}

uint32_t paging_kernel_directory(void) {
    return (uint32_t)page_directory;
}

#include "mutex.h"
#include "paging.h"

/* The first 16 MiB contains fixed kernel buffers and the boot stack.
 * Allocate only complete pages advertised as usable by BIOS E820.
 * RAM overlapping the private virtual heap window is reserved: kernel
 * physical accesses use identity mappings. PCI/MMIO above 2 GiB stays mapped.
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
#define PAGE_UNCACHED          0x018u /* PCD + PWT, PAT entry 3 (UC). */
#define PAGE_SIZE_4M           0x080u
#define PAGE_ALLOC_FIRST       0x200u
#define PAGE_ALLOC_LAST        0x400u
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
    for (uint32_t i = 0; i < FRAME_COUNT; ++i) {
        if (!frame_is_usable(i)) continue;
        detected_total += PAGE_SIZE;
        uint32_t physical = FRAME_POOL_BASE + i * PAGE_SIZE;
        if (physical >= APP_HEAP_BASE && physical < APP_HEAP_LIMIT)
            usable_bitmap[i >> 3] &= (uint8_t)~(1u << (i & 7u));
        else ++pool_pages;
    }
    next_frame = 0;
}

static uint32_t active_directory;
/* Split MMIO tables belong to the kernel and are shared, never app-owned. */
static uint8_t mmio_directory[PAGE_DIRECTORY_ENTRIES];
static uint16_t mmio_indices[PAGE_DIRECTORY_ENTRIES];
static unsigned mmio_count;

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

int paging_map_mmio_page(uint32_t physical) {
    MUTEX_GUARD(kernel_mutex);
    if (physical < APP_HEAP_LIMIT || (physical & (PAGE_SIZE - 1u)) ||
        (physical < FRAME_POOL_LIMIT &&
         frame_is_usable((physical - FRAME_POOL_BASE) / PAGE_SIZE))) return 0;
    uint32_t d = physical >> 22;
    uint32_t entry = page_directory[d];
    uint32_t* table;
    if (entry & PAGE_SIZE_4M) {
        uint32_t frame = paging_frame_alloc();
        if (!frame) return 0;
        table = (uint32_t*)frame;
        for (uint32_t i = 0; i < PAGE_TABLE_ENTRIES; i++)
            table[i] = ((d << 22) + i * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITABLE;
    } else {
        if (!mmio_directory[d]) return 0;
        table = (uint32_t*)(entry & PAGE_ADDRESS_MASK);
    }
    uint32_t i = (physical >> 12) & 1023u;
    if ((table[i] & PAGE_UNCACHED) == PAGE_UNCACHED) return 1;
    uint32_t flags = task_irq_save();
    /* Evict old cache lines before changing the identity mapping's type. */
    __asm__ volatile ("wbinvd" ::: "memory");
    table[i] |= PAGE_UNCACHED;
    page_directory[d] = (uint32_t)table | PAGE_PRESENT | PAGE_WRITABLE;
    if (!mmio_directory[d]) mmio_indices[mmio_count++] = d;
    mmio_directory[d] = 1;
    ((uint32_t*)active_directory)[d] = page_directory[d];
    __asm__ volatile ("mov %0, %%cr3" :: "r"(active_directory) : "memory");
    task_irq_restore(flags);
    return 1;
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

    /* Apps use a private 12-16 MiB slot, outside kernel global data. */
    uint32_t app_directory_index = APP_VIRTUAL_BASE >> 22;
    uint32_t app_page_count = (APP_VIRTUAL_LIMIT - APP_VIRTUAL_BASE) / PAGE_SIZE;
    for (uint32_t i = 0; i < PAGE_TABLE_ENTRIES; i++) {
        uint32_t physical_address = (app_directory_index << 22) + i * PAGE_SIZE;
        space->app_page_table[i] = physical_address | PAGE_PRESENT | PAGE_WRITABLE;
    }
    for (uint32_t i = 0; i < app_page_count; i++)
        space->app_page_table[i] = 0;
    space->directory[app_directory_index] = table_frame | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    for (uint32_t i = APP_HEAP_BASE >> 22; i < APP_HEAP_LIMIT >> 22; ++i)
        space->directory[i] = 0;
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

    for (uint32_t d = APP_HEAP_BASE >> 22; d < APP_HEAP_LIMIT >> 22; ++d) {
        if (!(space->directory[d] & PAGE_PRESENT)) continue;
        uint32_t* table = (uint32_t*)(space->directory[d] & PAGE_ADDRESS_MASK);
        for (uint32_t i = 0; i < PAGE_TABLE_ENTRIES; ++i)
            if (table[i] & PAGE_PRESENT) paging_frame_free(table[i] & PAGE_ADDRESS_MASK);
        paging_frame_free((uint32_t)table);
    }
    paging_frame_free((uint32_t)space->app_page_table);
    paging_frame_free((uint32_t)space->directory);
    space->app_page_table = 0;
    space->directory = 0;
}

/* All page-table frames remain accessible through the supervisor identity map. */
static uint32_t* heap_entry(uint32_t* directory, uint32_t address, int create) {
    uint32_t d = address >> 22;
    if (!(directory[d] & PAGE_PRESENT)) {
        if (!create) return 0;
        uint32_t frame = paging_frame_alloc();
        if (!frame) return 0;
        directory[d] = frame | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }
    uint32_t* table = (uint32_t*)(directory[d] & PAGE_ADDRESS_MASK);
    return &table[(address >> 12) & 1023u];
}

static void heap_release(uint32_t* directory, uint32_t address, uint32_t pages) {
    for (uint32_t i = 0; i < pages; ++i) {
        uint32_t* entry = heap_entry(directory, address + i * PAGE_SIZE, 0);
        if (entry && (*entry & PAGE_PRESENT)) {
            paging_frame_free(*entry & PAGE_ADDRESS_MASK);
            *entry = 0;
        }
    }
    /* Reclaim empty tables too, including a table created before OOM. */
    for (uint32_t d = APP_HEAP_BASE >> 22; d < APP_HEAP_LIMIT >> 22; ++d) {
        if (!(directory[d] & PAGE_PRESENT)) continue;
        uint32_t* table = (uint32_t*)(directory[d] & PAGE_ADDRESS_MASK);
        uint32_t i = 0;
        while (i < PAGE_TABLE_ENTRIES && !table[i]) ++i;
        if (i == PAGE_TABLE_ENTRIES) {
            directory[d] = 0;
            paging_frame_free((uint32_t)table);
        }
    }
    paging_activate_directory((uint32_t)directory);
}

uint32_t paging_user_alloc(uint32_t bytes) {
    MUTEX_GUARD(kernel_mutex);
    if (!bytes || bytes > APP_HEAP_LIMIT - APP_HEAP_BASE ||
        active_directory == (uint32_t)page_directory) return 0;
    uint32_t pages = (bytes + PAGE_SIZE - 1u) / PAGE_SIZE;
    uint32_t* directory = (uint32_t*)active_directory;
    uint32_t run = 0, start = APP_HEAP_BASE;
    for (uint32_t address = APP_HEAP_BASE; address < APP_HEAP_LIMIT; address += PAGE_SIZE) {
        uint32_t* entry = heap_entry(directory, address, 0);
        if (entry && (*entry & PAGE_PRESENT)) { run = 0; continue; }
        if (!run) start = address;
        if (++run != pages) continue;
        for (uint32_t i = 0; i < pages; ++i) {
            uint32_t* target = heap_entry(directory, start + i * PAGE_SIZE, 1);
            uint32_t frame = target ? paging_frame_alloc() : 0;
            if (!frame) { heap_release(directory, start, i); return 0; }
            *target = frame | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER |
                (i == 0 ? PAGE_ALLOC_FIRST : 0) |
                (i + 1 == pages ? PAGE_ALLOC_LAST : 0);
        }
        paging_activate_directory((uint32_t)directory);
        return start;
    }
    return 0;
}

int paging_user_free(uint32_t address) {
    MUTEX_GUARD(kernel_mutex);
    if (address < APP_HEAP_BASE || address >= APP_HEAP_LIMIT ||
        (address & (PAGE_SIZE - 1u)) || active_directory == (uint32_t)page_directory)
        return -1;
    uint32_t* directory = (uint32_t*)active_directory;
    uint32_t* entry = heap_entry(directory, address, 0);
    if (!entry || !(*entry & PAGE_ALLOC_FIRST)) return -1;
    uint32_t pages = 0;
    for (uint32_t current = address; current < APP_HEAP_LIMIT; current += PAGE_SIZE) {
        entry = heap_entry(directory, current, 0);
        if (!entry || !(*entry & PAGE_PRESENT)) return -1;
        ++pages;
        if (*entry & PAGE_ALLOC_LAST) {
            heap_release(directory, address, pages);
            return 0;
        }
    }
    return -1;
}

void paging_activate(address_space_t* space) {
    if (!space || !space->directory) return;
    paging_activate_directory((uint32_t)space->directory);
}

void paging_activate_directory(uint32_t directory_address) {
    if (!directory_address || (directory_address & (PAGE_SIZE - 1u))) return;
    uint32_t flags = task_irq_save();
    /* Apps created before a late PCI probe also inherit its MMIO tables. */
    if (directory_address != (uint32_t)page_directory)
        for (unsigned i = 0; i < mmio_count; i++) {
            unsigned d = mmio_indices[i];
            ((uint32_t*)directory_address)[d] = page_directory[d];
        }
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

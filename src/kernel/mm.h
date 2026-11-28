#ifndef KERNEL_MM_H
#define KERNEL_MM_H

#include "types.h"
#include "arch/arch.h"
#include "../system/user/memory_api.h"

void paging_memory_info(user_memory_info_t* out);

/* Physical frames are discovered from the bootloader BIOS memory map. */
#define PAGE_SIZE 4096u
#define APP_VIRTUAL_BASE 0x00C00000u
#define APP_VIRTUAL_LIMIT 0x01000000u
/* Private heap window; keep the upper PCI/MMIO identity mappings intact. */
#define APP_HEAP_BASE  0x40000000u
#define APP_HEAP_LIMIT 0x80000000u

/* Enable the kernel's supervisor-only identity map. */
void paging_init(const arch_boot_info_t* boot);
uint8_t paging_is_enabled(void);
/* Map one aligned PCI register page uncached in every address space.
 * Only non-RAM above the private app heap is accepted. Returns 0 on failure. */
int paging_map_mmio_page(uint32_t physical);

/* Allocate and release 4 KiB physical frames from the reserved app pool. */
uint32_t paging_frame_alloc(void);
/* Identity-mapped contiguous RAM for kernel graphics and DMA buffers. */
uint32_t paging_alloc_contiguous(uint32_t pages);
void paging_frame_free(uint32_t physical_address);

/* Page-granular, zero-filled allocations owned by the active app.
 * Free accepts only the original allocation base. Zero/-1 indicate failure. */
uint32_t paging_user_alloc(uint32_t bytes);
int paging_user_free(uint32_t address);

#endif

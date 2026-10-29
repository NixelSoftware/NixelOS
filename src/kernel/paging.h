#ifndef KERNEL_PAGING_H
#define KERNEL_PAGING_H

#include "types.h"
#include "../system/user/memory_api.h"

void paging_memory_info(user_memory_info_t* out);

/* Physical frames are discovered from the bootloader BIOS memory map. */
#define PAGE_SIZE 4096u
#define APP_VIRTUAL_BASE 0x00C00000u
#define APP_VIRTUAL_LIMIT 0x00E00000u

typedef struct {
    uint32_t* directory;
    uint32_t* app_page_table;
} address_space_t;

/* Enable the kernel's supervisor-only identity map. */
void paging_init(void);
uint8_t paging_is_enabled(void);

/* Allocate and release 4 KiB physical frames from the reserved app pool. */
uint32_t paging_frame_alloc(void);
/* Identity-mapped contiguous RAM for kernel graphics and DMA buffers. */
uint32_t paging_alloc_contiguous(uint32_t pages);
void paging_frame_free(uint32_t physical_address);

/* Create isolated app mappings at [APP_VIRTUAL_BASE, APP_VIRTUAL_LIMIT). */
int paging_address_space_create(address_space_t* space);
int paging_address_space_alloc_page(address_space_t* space, uint32_t virtual_address,
                                   uint8_t writable, uint32_t* physical_out);
void paging_address_space_destroy(address_space_t* space);

/* CR3 helpers; callers must restore the kernel directory before returning. */
void paging_activate(address_space_t* space);
void paging_activate_directory(uint32_t directory_address);
void paging_activate_kernel(void);
uint32_t paging_current_directory(void);
uint32_t paging_kernel_directory(void);

#endif

#ifndef KERNEL_ARCH_I386_PAGING_H
#define KERNEL_ARCH_I386_PAGING_H

#include "../../mm.h"

typedef struct {
    uint32_t* directory;
    uint32_t* app_page_table;
} address_space_t;

/* Create isolated app mappings at [APP_VIRTUAL_BASE, APP_VIRTUAL_LIMIT). */
int paging_address_space_create(address_space_t* space);
int paging_address_space_alloc_page(address_space_t* space, uint32_t virtual_address,
                                   uint8_t writable, uint32_t* physical_out);
void paging_address_space_destroy(address_space_t* space);

#endif

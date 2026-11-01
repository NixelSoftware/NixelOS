#ifndef NIXEL2LINUX_ELF_H
#define NIXEL2LINUX_ELF_H

#include "../types.h"
#include "../paging.h"

#define ELF_MAGIC 0x464C457Fu  /* 0x7F, 'E', 'L', 'F' */

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) Elf32_Ehdr;

#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) Elf32_Phdr;

/* Returns 1 if binary header matches 32-bit x86 ELF */
int linux_is_elf(const uint8_t* header_bytes);

/* Prepares address space mappings, loads PT_LOAD segments, and builds Linux stack */
int linux_load_elf(const char* path, address_space_t* space,
                   uint32_t* out_entry, uint32_t* out_esp, uint32_t* out_brk);

#endif
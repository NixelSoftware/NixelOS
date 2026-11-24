#include "linux_elf.h"
#include "../fs.h"
#include "../paging.h"
#include "../drivers/serial.h"

int linux_is_elf(const uint8_t* header_bytes) {
    if (!header_bytes) return 0;
    const Elf32_Ehdr* ehdr = (const Elf32_Ehdr*)header_bytes;
    return (*(const uint32_t*)ehdr->e_ident == ELF_MAGIC) &&
           (ehdr->e_ident[4] == 1) && /* 32-bit (ELFCLASS32) */
           (ehdr->e_ident[5] == 1) && /* Little-endian */
           (ehdr->e_machine == 3);    /* x86 (EM_386) */
}

int linux_load_elf(const char* path, address_space_t* space,
                   uint32_t* out_entry, uint32_t* out_esp, uint32_t* out_brk) {
    Elf32_Ehdr ehdr;
    if (fs_read_file_at(path, (uint8_t*)&ehdr, sizeof(ehdr), 0) != sizeof(ehdr)) {
        return -1;
    }

    if (!linux_is_elf((const uint8_t*)&ehdr)) {
        return -2;
    }

    uint32_t highest_vaddr = 0;

    /* Map and load all PT_LOAD segments */
    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        Elf32_Phdr phdr;
        uint32_t phdr_offset = ehdr.e_phoff + (i * sizeof(Elf32_Phdr));
        if (fs_read_file_at(path, (uint8_t*)&phdr, sizeof(phdr), phdr_offset) != sizeof(phdr)) {
            return -1;
        }

        if (phdr.p_type != PT_LOAD) continue;

        uint32_t vaddr_start = phdr.p_vaddr & ~(PAGE_SIZE - 1u);
        uint32_t vaddr_end   = (phdr.p_vaddr + phdr.p_memsz + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);

        /* Map with writable=1, physical_out=0 */
        for (uint32_t page = vaddr_start; page < vaddr_end; page += PAGE_SIZE) {
            paging_address_space_alloc_page(space, page, 1, 0);
        }

        /* Read segment bytes directly into virtual memory */
        if (phdr.p_filesz > 0) {
            fs_read_file_at(path, (uint8_t*)phdr.p_vaddr, phdr.p_filesz, phdr.p_offset);
        }

        /* Zero out BSS tail if memsz > filesz */
        if (phdr.p_memsz > phdr.p_filesz) {
            uint8_t* bss = (uint8_t*)(phdr.p_vaddr + phdr.p_filesz);
            uint32_t bss_len = phdr.p_memsz - phdr.p_filesz;
            for (uint32_t b = 0; b < bss_len; b++) {
                bss[b] = 0;
            }
        }

        if (phdr.p_vaddr + phdr.p_memsz > highest_vaddr) {
            highest_vaddr = phdr.p_vaddr + phdr.p_memsz;
        }
    }

    /* Map standard Linux 32-bit User Stack near 0xBFFFF000 (writable=1, physical_out=0) */
    uint32_t stack_base = 0xBFF00000u;
    uint32_t stack_top  = 0xBFFFF000u;
    for (uint32_t p = stack_base; p < stack_top; p += PAGE_SIZE) {
        paging_address_space_alloc_page(space, p, 1, 0);
    }

    /* Program brk directly above data/bss */
    *out_brk = (highest_vaddr + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
    *out_entry = ehdr.e_entry;

    /* Construct System V ABI Initial Stack Layout with AUX vectors */
    uint32_t* sp = (uint32_t*)stack_top;

    int path_len = 0;
    while (path[path_len]) path_len++;
    char* str_ptr = (char*)sp - (path_len + 1);
    for (int i = 0; i <= path_len; i++) str_ptr[i] = path[i];
    sp = (uint32_t*)((uint32_t)str_ptr & ~3u);

    /* Auxiliary Vector Table */
    *(--sp) = 0; *(--sp) = 0;               /* AT_NULL */
    *(--sp) = ehdr.e_entry; *(--sp) = 9;    /* AT_ENTRY */
    *(--sp) = 4096; *(--sp) = 6;            /* AT_PAGESZ */
    *(--sp) = 1000; *(--sp) = 11;           /* AT_UID */
    *(--sp) = 1000; *(--sp) = 12;           /* AT_EUID */
    *(--sp) = 1000; *(--sp) = 13;           /* AT_GID */
    *(--sp) = 1000; *(--sp) = 14;           /* AT_EGID */

    /* Env pointer array */
    *(--sp) = 0;                            /* envp[0] = NULL */

    /* Argv pointer array */
    *(--sp) = 0;                            /* argv[1] = NULL */
    *(--sp) = (uint32_t)str_ptr;            /* argv[0] */

    /* Argc */
    *(--sp) = 1;                            /* argc = 1 */

    *out_esp = (uint32_t)sp;
    return 0;
}
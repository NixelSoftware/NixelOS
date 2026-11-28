#include "types.h"
#include "mmu.h"
#include "arch/arch.h"

#define L1_ENTRIES    4096u
#define SECTION_SIZE  0x00100000u
#define RAM_BASE      0x40000000u
#define RAM_SIZE      0x10000000u

#define SEC_NORMAL    0x1C0Eu
#define SEC_DEVICE    0xC16u

#define USER_WINDOW_BASE 0x20000000u
#define USER_SPACES      4u

static uint32_t l1_table[L1_ENTRIES] __attribute__((aligned(16384)));
static uint32_t user_window[0x40000u] __attribute__((aligned(0x100000)));
static uint32_t user_spaces[USER_SPACES][L1_ENTRIES] __attribute__((aligned(16384)));
static int user_space_used[USER_SPACES];
static uint32_t active_root;

static inline void write_ttbr0(uint32_t value) {
    __asm__ volatile ("mcr p15, 0, %0, c2, c0, 0" : : "r"(value) : "memory");
}

static inline void write_ttbcr(uint32_t value) {
    __asm__ volatile ("mcr p15, 0, %0, c2, c0, 2" : : "r"(value) : "memory");
}

static inline void write_dacr(uint32_t value) {
    __asm__ volatile ("mcr p15, 0, %0, c3, c0, 0" : : "r"(value) : "memory");
}

static inline uint32_t read_sctlr(void) {
    uint32_t value;
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(value));
    return value;
}

static inline void write_sctlr(uint32_t value) {
    __asm__ volatile ("mcr p15, 0, %0, c1, c0, 0" : : "r"(value) : "memory");
}

static void map_section(uint32_t address, uint32_t attributes) {
    l1_table[address >> 20] = (address & 0xFFF00000u) | attributes;
}

static void map_range(uint32_t base, uint32_t size, uint32_t attributes) {
    for (uint32_t address = base; address < base + size; address += SECTION_SIZE)
        map_section(address, attributes);
}

void arm_mmu_init(void) {
    for (uint32_t i = 0; i < L1_ENTRIES; i++)
        l1_table[i] = 0;

    map_range(0x08000000u, 0x00200000u, SEC_DEVICE);
    map_range(0x09000000u, 0x00100000u, SEC_DEVICE);
    map_range(0x0A000000u, 0x00100000u, SEC_DEVICE);
    map_range(RAM_BASE, RAM_SIZE, SEC_NORMAL);

    write_ttbcr(0);
    write_ttbr0((uint32_t)l1_table);
    write_dacr(0x55555555u);

    __asm__ volatile ("dsb" : : : "memory");
    __asm__ volatile ("isb" : : : "memory");

    uint32_t sctlr = read_sctlr();
    sctlr |= (1u << 0) | (1u << 2) | (1u << 12);
    write_sctlr(sctlr);

    active_root = (uint32_t)l1_table;

    __asm__ volatile ("dsb" : : : "memory");
    __asm__ volatile ("isb" : : : "memory");
}

uint32_t arch_current_address_space(void) {
    return active_root;
}

uint32_t arch_kernel_address_space(void) {
    return (uint32_t)l1_table;
}

void arch_flush_tlb(void) {
    uint32_t zero = 0;
    __asm__ volatile ("mcr p15, 0, %0, c8, c7, 0" : : "r"(zero) : "memory");
    __asm__ volatile ("dsb" : : : "memory");
    __asm__ volatile ("isb" : : : "memory");
}

void arch_switch_address_space(uint32_t root_phys) {
    if (!root_phys) root_phys = (uint32_t)l1_table;
    if (root_phys == active_root) return;
    active_root = root_phys;
    write_ttbr0(root_phys);
    arch_flush_tlb();
}

uint32_t* arm_space_create_user(void) {
    for (uint32_t i = 0; i < USER_SPACES; i++) {
        if (user_space_used[i]) continue;
        user_space_used[i] = 1;
        uint32_t* space = user_spaces[i];
        for (uint32_t entry = 0; entry < L1_ENTRIES; entry++)
            space[entry] = l1_table[entry];
        space[USER_WINDOW_BASE >> 20] = (uint32_t)user_window | SEC_NORMAL;
        return space;
    }
    return 0;
}

uint32_t arm_mmu_sctlr(void) {
    return read_sctlr();
}

arch_address_space_t* arch_address_space_create(void) {
    return 0;
}

void arch_address_space_destroy(arch_address_space_t* space) {
    (void)space;
}

int arch_address_space_map(arch_address_space_t* space, uint32_t virt,
                           uint8_t writable, uint32_t* phys_out) {
    (void)space; (void)virt; (void)writable; (void)phys_out;
    return 0;
}

uint32_t arch_address_space_root(const arch_address_space_t* space) {
    (void)space;
    return 0;
}

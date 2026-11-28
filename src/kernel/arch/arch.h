#ifndef NIXEL_ARCH_H
#define NIXEL_ARCH_H

/*
 * Neutral CPU-architecture interface for NixelOS.
 *
 * The generic kernel (scheduler, syscalls, memory, drivers) targets THIS
 * contract. Each CPU architecture provides an implementation under
 * src/kernel/arch/<cpu>/ (i386 today; arm next). Nothing above this header
 * should name a specific architecture.
 */

#include "../types.h"

/* ------------------------------------------------------------------ */
/* Boot handoff                                                        */
/* ------------------------------------------------------------------ */
/*
 * Filled by the board/loader layer and consumed by the generic kernel, so the
 * kernel never reads a BIOS/loader-specific structure directly. The x86 BIOS
 * stage2 fills this; ARM boards fill it from their loader (OpenIBoot / U-Boot /
 * QEMU -kernel).
 */
#define ARCH_BOOT_MAX_MEM_REGIONS 64u

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t usable; /* 1 = normal RAM the frame allocator may use */
} arch_mem_region_t;

typedef struct {
    uint32_t fb_addr;   /* linear framebuffer physical address */
    uint32_t fb_pitch;  /* bytes per scanline */
    uint32_t fb_bpp;    /* bits per pixel */
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t flags;
    uint32_t region_count;
    arch_mem_region_t regions[ARCH_BOOT_MAX_MEM_REGIONS];
    const char* cmdline;
} arch_boot_info_t;

/* ------------------------------------------------------------------ */
/* CPU lifecycle                                                       */
/* ------------------------------------------------------------------ */
void arch_init(void);                              /* descriptor tables / MMU base / traps */
void arch_trap_init(void);
void arch_halt(void);                              /* idle the CPU until the next interrupt */
__attribute__((noreturn)) void arch_halt_forever(void);
void arch_set_kernel_stack(uint32_t stack_top);    /* stack used on the next kernel entry */
void arch_enter_user(uint32_t entry, uint32_t user_stack);
extern volatile uint32_t arch_user_exit_requested;

#define ARCH_SYSCALL_ARGS 4u

typedef struct {
    uint32_t number;
    uint32_t arg[ARCH_SYSCALL_ARGS];
    uint32_t result;
    int from_user;
} arch_syscall_t;

/* ------------------------------------------------------------------ */
/* Interrupts                                                          */
/* ------------------------------------------------------------------ */
void arch_irq_enable(void);
void arch_irq_disable(void);
uint32_t arch_irq_save(void);
void arch_irq_restore(uint32_t state);
int arch_irq_were_enabled(uint32_t state);

typedef int (*arch_irq_handler_t)(void* ctx);
int  arch_irq_register(uint32_t irq, arch_irq_handler_t handler, void* ctx);
void arch_irq_unregister(uint32_t irq);
int  arch_irq_active(uint32_t irq);
void arch_irq_eoi(uint32_t irq);                  /* acknowledge at the controller */

/* ------------------------------------------------------------------ */
/* Virtual memory                                                      */
/* ------------------------------------------------------------------ */
/* flags are architecture-neutral bits below. */
#define ARCH_MAP_WRITE   (1u << 0)
#define ARCH_MAP_USER    (1u << 1)
#define ARCH_MAP_NOCACHE (1u << 2)

int  arch_map_page(uint32_t virt, uint32_t phys, uint32_t flags);
int  arch_unmap_page(uint32_t virt);
void arch_cache_flush(void);
uint32_t arch_current_address_space(void);
uint32_t arch_kernel_address_space(void);
void arch_switch_address_space(uint32_t root_phys); /* load the page-table root */
void arch_flush_tlb(void);

typedef struct arch_address_space arch_address_space_t;
arch_address_space_t* arch_address_space_create(void);
void arch_address_space_destroy(arch_address_space_t* space);
int  arch_address_space_map(arch_address_space_t* space, uint32_t virt,
                            uint8_t writable, uint32_t* phys_out);
uint32_t arch_address_space_root(const arch_address_space_t* space);

/* ------------------------------------------------------------------ */
/* Task context switch                                                 */
/* ------------------------------------------------------------------ */
/* Opaque per-task saved register state; the arch defines the real layout. */
struct arch_context;
void arch_context_switch(struct arch_context* prev, struct arch_context* next);

/* ------------------------------------------------------------------ */
/* Monotonic timer                                                     */
/* ------------------------------------------------------------------ */
void arch_timer_init(uint32_t hz);
void arch_timer_start(uint32_t hz);
int  arch_timer_started(void);
uint64_t arch_timer_ticks(void);
void arch_timer_irq_tick(void);
void arch_timer_set_rate(uint32_t hz);
uint32_t arch_timer_clock_source(void);
uint32_t arch_timer_cycles_per_us(void);

#endif /* NIXEL_ARCH_H */

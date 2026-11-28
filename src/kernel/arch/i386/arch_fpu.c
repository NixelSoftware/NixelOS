#include "arch_fpu.h"
static uint32_t features;
static int has_cpuid(void) {
    uint32_t original, changed;
    __asm__ volatile ("pushfl; popl %0" : "=r"(original));
    changed = original ^ (1u << 21);
    __asm__ volatile ("pushl %0; popfl; pushfl; popl %0" : "+r"(changed) : : "memory", "cc");
    __asm__ volatile ("pushl %0; popfl" : : "r"(original) : "memory", "cc");
    return ((changed ^ original) & (1u << 21)) != 0;
}
uint32_t arch_fpu_features(void) { return features; }
void arch_fpu_save(arch_fpu_state_t* state) {
    if (features & ARCH_FPU_FXSR)
        __asm__ volatile ("fxsave %0" : "=m"(*state) : : "memory");
    else
        __asm__ volatile ("fnsave %0" : "=m"(*state) : : "memory");
}
void arch_fpu_restore(const arch_fpu_state_t* state) {
    if (features & ARCH_FPU_FXSR)
        __asm__ volatile ("fxrstor %0" : : "m"(*state) : "memory");
    else
        __asm__ volatile ("frstor %0" : : "m"(*state) : "memory");
}
uint32_t arch_fpu_init(arch_fpu_state_t* initial) {
    uint32_t caps = 1, a, b, c, d, cr;
    features = 0;
    int cpuid = has_cpuid();
    /* Pre-CPUID processors retain the existing requirement for an x87 FPU. */
    if (cpuid) {
        a = 0;
        __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
        if (a < 1) return 0;
        a = 1;
        __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
        caps = d;
        if (!(caps & 1u)) return 0;
    }
    features = ARCH_FPU_X87;
    if (caps & (1u << 23)) features |= ARCH_FPU_MMX;
    /* Select the extended format only with both FXSR and SSE. FXSR-only
     * CPUs need no XMM state and use the legacy x87/MMX path. */
    if ((caps & ((1u << 24) | (1u << 25))) == ((1u << 24) | (1u << 25)))
        features |= ARCH_FPU_FXSR | ARCH_FPU_SSE;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr));
    cr = (cr & ~((1u << 2) | (1u << 3))) | (1u << 1) | (1u << 5);
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr) : "memory");
    if (cpuid) {
        __asm__ volatile ("mov %%cr4, %0" : "=r"(cr));
        /* No OSXSAVE: exposing AVX would require saving YMM and XCR0 state. */
        cr &= ~((1u << 9) | (1u << 10) | (1u << 18));
        if (features & ARCH_FPU_FXSR) cr |= 1u << 9;
        if (features & ARCH_FPU_SSE) cr |= 1u << 10;
        __asm__ volatile ("mov %0, %%cr4" : : "r"(cr) : "memory");
    }
    /* Clear any bootloader/firmware pending x87 exception before FRSTOR. */
    __asm__ volatile ("fninit" : : : "memory");
    for (unsigned i = 0; i < sizeof(*initial); ++i) initial->bytes[i] = 0;
    initial->bytes[0] = 0x7f; initial->bytes[1] = 0x03; /* FCW: masked, nearest */
    if (features & ARCH_FPU_FXSR) {
        initial->bytes[24] = 0x80; initial->bytes[25] = 0x1f; /* MXCSR */
        /* FTW=0: all x87 registers empty. Register payloads/XMM are zero. */
    } else {
        initial->bytes[8] = initial->bytes[9] = 0xff; /* full empty tag word */
    }
    arch_fpu_restore(initial);
    return features;
}

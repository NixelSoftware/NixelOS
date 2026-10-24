#ifndef KERNEL_ARCH_FPU_H
#define KERNEL_ARCH_FPU_H
#include "types.h"
#define ARCH_FPU_X87 1u
#define ARCH_FPU_FXSR 2u
#define ARCH_FPU_SSE 4u
#define ARCH_FPU_MMX 8u
/* FXSAVE's legacy i386 format includes x87/MMX, MXCSR and XMM0..7.
 * The same aligned storage holds the 108-byte legacy x87 image on older CPUs. */
typedef struct { uint8_t bytes[512]; } __attribute__((aligned(16))) arch_fpu_state_t;
_Static_assert(sizeof(arch_fpu_state_t) == 512, "FXSAVE storage size");
_Static_assert(__alignof__(arch_fpu_state_t) >= 16, "FXSAVE alignment");
/* Boot only, IRQs off. Initializes a clean task template and the live state.
 * Returns zero if CPUID reports no hardware FPU (no software emulation). */
uint32_t arch_fpu_init(arch_fpu_state_t* initial);
uint32_t arch_fpu_features(void);
/* Context-switch boundary only, IRQs off. IRQ handlers and kernel C code
 * must not borrow application FP/SIMD registers. FNSAVE resets the legacy FPU. */
void arch_fpu_save(arch_fpu_state_t* state);
void arch_fpu_restore(const arch_fpu_state_t* state);
#endif

#ifndef KERNEL_ARCH_I386_ARCH_CONTEXT_H
#define KERNEL_ARCH_I386_ARCH_CONTEXT_H

#include "../../types.h"
#include "arch_fpu.h"

/* task_switch.asm timer/yield ABI, NOT the exception/syscall trap frame.
 * Hardware appends user ESP/SS only when returning across privilege levels. */
typedef struct task_switch_frame {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t eip, cs, eflags;
} task_switch_frame_t;
_Static_assert(sizeof(task_switch_frame_t) == 60, "switch frame size");
_Static_assert(__builtin_offsetof(task_switch_frame_t, edi) == 16, "PUSHAD offset");
_Static_assert(__builtin_offsetof(task_switch_frame_t, eip) == 48, "IRET offset");
_Static_assert(__builtin_offsetof(task_switch_frame_t, cs) == 52, "saved CS offset");
_Static_assert(__builtin_offsetof(task_switch_frame_t, eflags) == 56, "saved flags offset");

typedef struct {
    task_switch_frame_t kernel;
    uint32_t useresp, ss;
} task_user_switch_frame_t;
_Static_assert(sizeof(task_user_switch_frame_t) == 68, "user switch frame size");
_Static_assert(__builtin_offsetof(task_user_switch_frame_t, useresp) == 60, "user ESP offset");

struct arch_context {
    uint32_t esp;
    uint32_t user_kernel_esp;
    uint32_t user_exit_requested;
    uint32_t reserved;
    arch_fpu_state_t fpu_state;
};
_Static_assert(__builtin_offsetof(struct arch_context, esp) == 0,
               "task_switch.asm ESP offset");
_Static_assert(__builtin_offsetof(struct arch_context, fpu_state) % 16 == 0,
               "arch context FPU alignment");
_Static_assert(sizeof(struct arch_context) % 16 == 0, "arch context size");

#endif

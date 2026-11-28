.syntax unified
.arm
.arch armv7-a

.section .text.startup, "ax"
.global _start
.type _start, %function

_start:
    cpsid   if
    mov     r4, r2

    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
_bss_loop:
    cmp     r0, r1
    strlo   r2, [r0], #4
    blo     _bss_loop

    ldr     sp, =_stack_top

    mrs     r0, cpsr
    bic     r1, r0, #0x1F
    orr     r2, r1, #0x12
    msr     cpsr_c, r2
    ldr     sp, =_irq_stack_top
    orr     r2, r1, #0x11
    msr     cpsr_c, r2
    ldr     sp, =_fiq_stack_top
    orr     r2, r1, #0x17
    msr     cpsr_c, r2
    ldr     sp, =_abt_stack_top
    orr     r2, r1, #0x1B
    msr     cpsr_c, r2
    ldr     sp, =_und_stack_top
    msr     cpsr_c, r0

    ldr     r0, =_vectors
    mcr     p15, 0, r0, c12, c0, 0
    isb

    mov     r0, r4
    bl      arm_main

_halt:
    wfi
    b       _halt
.ltorg

.section .bss
.align 4
_stack_bottom:
    .skip   16384
_stack_top:
_irq_stack_bottom:
    .skip   8192
_irq_stack_top:
_fiq_stack_bottom:
    .skip   2048
_fiq_stack_top:
_abt_stack_bottom:
    .skip   2048
_abt_stack_top:
_und_stack_bottom:
    .skip   2048
_und_stack_top:

.section .note.GNU-stack, "", %progbits

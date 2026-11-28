.syntax unified
.arm

.section .text.vectors, "ax"
.align 5
.global _vectors

_vectors:
    b   _vec_reset
    b   _vec_undef
    b   _vec_svc
    b   _vec_prefetch
    b   _vec_data
    b   _vec_hyp
    b   _vec_irq
    b   _vec_fiq

_vec_reset:
    mov r0, #0
    mov r1, #0
    b   _vec_common
_vec_undef:
    mov r0, #1
    mov r1, lr
    b   _vec_common
_vec_svc:
    b   _svc_entry
_vec_prefetch:
    mov r0, #3
    mov r1, lr
    b   _vec_common
_vec_data:
    mov r0, #4
    mov r1, lr
    b   _vec_common
_vec_hyp:
    mov r0, #5
    mov r1, #0
    b   _vec_common
_vec_irq:
    b   _irq_entry
_vec_fiq:
    mov r0, #7
    mov r1, lr
    b   _vec_common

_vec_common:
    bl  arm_exception
1:  wfi
    b   1b

_irq_entry:
    sub     lr, lr, #4
    srsdb   sp!, #0x13
    cpsid   i
    cps     #0x13
    push    {r0-r12, lr}
    mov     r0, sp
    bl      arm_irq_dispatch
    mov     sp, r0
    pop     {r0-r12, lr}
    rfeia   sp!
.ltorg

_svc_entry:
    push    {r4-r6, lr}
    mov     r4, r0
    mov     r5, r1
    mov     r6, r2
    mov     r0, r7
    mov     r1, r4
    mov     r2, r5
    mov     r3, r6
    bl      arm_syscall
    pop     {r4-r6, lr}
    movs    pc, lr
.ltorg

.section .note.GNU-stack, "", %progbits

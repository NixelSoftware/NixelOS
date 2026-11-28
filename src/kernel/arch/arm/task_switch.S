.syntax unified
.arm

.section .text, "ax"
.global task_yield
.global _task_start
.type task_yield, %function

task_yield:
    sub     sp, sp, #64
    stmia   sp, {r0-r12, lr}
    ldr     r0, =.Lyield_resume
    str     r0, [sp, #56]
    mrs     r0, cpsr
    str     r0, [sp, #60]
    ldr     r1, =current_task
    ldr     r1, [r1]
    str     sp, [r1]
    bl      task_schedule_next
    ldr     r1, =current_task
    ldr     r1, [r1]
    ldr     sp, [r1]
    pop     {r0-r12, lr}
    rfeia   sp!
.Lyield_resume:
    bx      lr
.ltorg

_task_start:
    mov     r0, r4
    blx     r5
    bl      task_exit
1:  b       1b

.global arch_enter_user
.type arch_enter_user, %function

arch_enter_user:
    mrs     r2, cpsr
    cpsid   i
    bic     r2, r2, #0x1F
    orr     r2, r2, #0x10
    msr     spsr_cxsf, r2
    cps     #0x1F
    mov     sp, r1
    cps     #0x13
    mov     lr, r0
    movs    pc, lr
.ltorg

.global user_syscall_stub
.type user_syscall_stub, %function

user_syscall_stub:
    mov     r7, r0
    mov     r0, r1
    mov     r1, r2
    mov     r2, r3
    svc     #0
    bx      lr

.section .note.GNU-stack, "", %progbits

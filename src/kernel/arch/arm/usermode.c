#include "types.h"
#include "usermode.h"
#include "fbcon.h"
#include "mmu.h"
#include "task.h"
#include "timer.h"

#define SYS_WRITE     1u
#define SYS_GET_TICKS 2u
#define SYS_EXIT      3u
#define SYS_LOG_SP    4u

extern uint32_t user_syscall_stub(uint32_t number, uint32_t a0, uint32_t a1, uint32_t a2);

static void user_main(void) {
    uint32_t sp;
    __asm__ volatile ("mov %0, sp" : "=r"(sp));
    user_syscall_stub(SYS_LOG_SP, sp, 0, 0);
    user_syscall_stub(SYS_WRITE, (uint32_t)"hello from user mode\n", 0, 0);
    user_syscall_stub(SYS_WRITE, (uint32_t)"ARM PL0 svc ok\n", 0, 0);
    user_syscall_stub(SYS_EXIT, 0, 0, 0);
    for (;;) { }
}

void usermode_task(void* arg) {
    (void)arg;
    uint32_t* space = arm_space_create_user();
    if (!space) {
        fbcon_write("user: no address space\n");
        fbcon_flush();
        task_exit();
    }
    current_task->directory = (uint32_t)space;
    arch_switch_address_space((uint32_t)space);
    arch_enter_user((uint32_t)user_main, 0x20100000u);
}

static void log_hex(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    fbcon_write("0x");
    for (int shift = 28; shift >= 0; shift -= 4)
        fbcon_putc(digits[(value >> shift) & 0xFu]);
}

uint32_t arm_syscall(uint32_t number, uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a1;
    (void)a2;
    switch (number) {
        case SYS_WRITE:
            fbcon_write((const char*)a0);
            fbcon_flush();
            return 0;
        case SYS_GET_TICKS:
            return (uint32_t)(arch_timer_ticks() / 1000000ull);
        case SYS_LOG_SP:
            fbcon_write("user sp=");
            log_hex(a0);
            fbcon_write("\n");
            fbcon_flush();
            return 0;
        case SYS_EXIT:
            fbcon_write("\nuser: exited\n");
            fbcon_flush();
            task_exit();
        default:
            return (uint32_t)-1;
    }
}

#include "syscall.h"
#include "idt.h"
#include "paging.h"
#include "arch_i386.h"
#include "drivers/serial.h"

#define SYS_WRITE_MAX 256u
extern volatile uint32_t frame_timer;

static int user_readable(uint32_t address, uint32_t length) {
    if (!length || address < APP_VIRTUAL_BASE || address >= APP_VIRTUAL_LIMIT ||
        length > APP_VIRTUAL_LIMIT - address) return 0;

    uint32_t* directory = (uint32_t*)paging_current_directory();
    uint32_t end = address + length - 1u;
    for (uint32_t page = address & ~(PAGE_SIZE - 1u);;
         page += PAGE_SIZE) {
        uint32_t pde = directory[page >> 22];
        if ((pde & 0x5u) != 0x5u) return 0; /* present + user */
        uint32_t* table = (uint32_t*)(pde & 0xFFFFF000u);
        uint32_t pte = table[(page >> 12) & 0x3FFu];
        if ((pte & 0x5u) != 0x5u) return 0;
        if (page >= (end & ~(PAGE_SIZE - 1u))) break;
    }
    return 1;
}

void syscall_dispatch(registers_t* regs) {
    if (!regs) return;
    if ((regs->cs & 3u) != 3u) {
        regs->eax = (uint32_t)-1;
        return;
    }
    switch (regs->eax) {
        case SYS_WRITE: {
            uint32_t address = regs->ebx;
            uint32_t length = regs->ecx;
            if (length > SYS_WRITE_MAX || !user_readable(address, length)) {
                regs->eax = (uint32_t)-1;
                return;
            }
            const char* text = (const char*)address;
            for (uint32_t i = 0; i < length; i++) debug_putc(text[i]);
            regs->eax = length;
            return;
        }
        case SYS_GET_TICKS:
            regs->eax = frame_timer;
            return;
        case SYS_EXIT:
            arch_user_exit_requested = 1;
            return;
        default:
            regs->eax = (uint32_t)-1;
            return;
    }
}

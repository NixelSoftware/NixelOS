#include "power.h"
#include "serial.h"
#include "acpi.h"

void power_shutdown(void) {
    debug_print("[POWER] Initiating system shutdown...\n");
    acpi_power_off();
    debug_print("[POWER] ACPI Failed :(\n");

    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    outb(0x501, 0x00);

    __asm__ volatile (
        "cli\n\t"
        "1: hlt\n\t"
        "jmp 1b\n\t"
    );
}

void power_reboot(void) {
    debug_print("[POWER] Initiating system reboot...\n");

    uint8_t temp;
    do {
        temp = inb(0x64);
        if (temp & 1) (void)inb(0x60);
    } while (temp & 2);

    outb(0x64, 0xFE);

    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) null_idt = {0, 0};

    __asm__ volatile (
        "lidt %0\n\t"
        "int3\n\t"
        :
        : "m"(null_idt)
    );
}
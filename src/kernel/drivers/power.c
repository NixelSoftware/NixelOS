#include "power.h"
#include "serial.h"
#include "acpi.h"
#include "display.h"
#include "display/font.h"
#include "../../system/graphics/fonts/univers85ebo_16.h"

extern const uint8_t font_8x8[128][8];

void power_shutdown(void) {
    debug_print("[POWER] Initiating system shutdown...\n");
    acpi_power_off();
    debug_print("[POWER] ACPI Failed - Does this system support ACPI?\n");

    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    outb(0x501, 0x00);

    const char* shutdown_msg = "It is now safe to turn off your PC";
    int text_len = 0;
    while (shutdown_msg[text_len]) {
        text_len++;
    }

    int total_width = text_len * 16;
    int x = (SCREEN_WIDTH - total_width) / 2;
    int y = (SCREEN_HEIGHT - 16) / 2;

    display_clear(COLOR_BLACK);
    font1_draw_string_16(x, y, shutdown_msg, COLOR_RED);
    display_blit();

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
#include "emergency.h"
#include "drivers/display.h"
#include "drivers/display/font.h"
#include "drivers/serial.h"

static int emg_line = 1;

void emergency_console_clear(void) {
    __asm__ volatile ("cli");

    serial_enter_sync();

    display_clear(0x00000000);
    emg_line = 1;
}

void emergency_console_print(const char* text, uint32_t color) {
    if (!text) return;

    debug_print(text);
    debug_print("\n");

    int x = 16;
    int y = emg_line * 16;
    draw_string(x, y, text, color);
    emg_line++;

    display_blit();
}

__attribute__((noreturn)) void emergency_panic(const char* title, const char* message) {
    emergency_console_clear();

    emergency_console_print("=== NIXELOS KERNEL EMERGENCY ===", 0x00FF5555);
    emergency_console_print("", 0);
    if (title && *title) {
        emergency_console_print(title, 0x00FFFF55);
    }
    if (message && *message) {
        emergency_console_print(message, 0x00FFFFFF);
    }
    emergency_console_print("", 0);
    emergency_console_print("System halted. Please reboot the system.", 0x00888888);

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
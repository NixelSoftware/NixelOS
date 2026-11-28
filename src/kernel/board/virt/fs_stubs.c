#include "types.h"
#include "users.h"
#include "emergency.h"
#include "fbcon.h"

void emergency_console_clear(void) { }
void emergency_console_print(const char* text, uint32_t color) {
    (void)color;
    fbcon_write(text);
}

__attribute__((noreturn)) void emergency_panic(const char* title, const char* message) {
    fbcon_write("\n[PANIC] ");
    if (title) fbcon_write(title);
    if (message) {
        fbcon_write(": ");
        fbcon_write(message);
    }
    fbcon_write("\n");
    fbcon_flush();
    for (;;) __asm__ volatile ("wfi");
}

int ahci_flush(uint8_t port_no) {
    (void)port_no;
    return 0;
}

int kernel_entropy(void* context, uint8_t* out, unsigned length) {
    (void)context;
    for (unsigned i = 0; i < length; i++) out[i] = 0;
    return 0;
}

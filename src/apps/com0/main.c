#include "../../system/user/sys_api.h"

static void print(const char* text) {
    uint32_t n = 0; while (text[n]) ++n;
    (void)sys_write(text, n);
}
void user_main(const char* arguments) {
    uint32_t seconds = 0;
    while (*arguments == ' ') ++arguments;
    while (*arguments >= '0' && *arguments <= '9') {
        seconds = seconds * 10u + (uint32_t)(*arguments++ - '0');
        if (seconds > 3600) { print("Usage: com0 [seconds: 1..3600]\n"); return; }
    }
    while (*arguments == ' ') ++arguments;
    if (*arguments) { print("Usage: com0 [seconds: 1..3600]\n"); return; }
    uint32_t started = sys_get_ticks();
    print("COM0: following kernel serial output (first UART, COM1/0x3F8).\n");
    print("Open audio from Files; use another terminal to run commands.\n");
    user_serial_cursor_t cursor = {0};
    char bytes[256], text[256];
    while (!seconds || (uint32_t)(sys_get_ticks() - started) < seconds * 100u) {
        uint32_t previous_lost = cursor.lost;
        int32_t count = sys_serial_log_read(bytes, sizeof(bytes), &cursor);
        if (count < 0) { print("Serial log unavailable\n"); return; }
        if (cursor.lost != previous_lost) print("\n[COM0] Log overrun: older output lost.\n");
        uint32_t n = 0;
        for (int32_t i = 0; i < count; ++i) {
            /* The terminal handles LF; UART logs contain CRLF. */
            if (bytes[i] != '\r') text[n++] = bytes[i];
        }
        if (n) (void)sys_write(text, n);
        sys_yield();
    }
}

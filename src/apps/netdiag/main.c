#include "../../kernel/types.h"
#include "../../kernel/os_api.h"

static void print_u32(os_api_t* os, uint32_t value) {
    char digits[11];
    int count = 0;
    if (!value) {
        os->shell.term_print("0");
        return;
    }
    while (value) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (count) {
        char digit[2] = { digits[--count], '\0' };
        os->shell.term_print(digit);
    }
}

static void print_hex(os_api_t* os, uint32_t value, int bytes) {
    const char* digits = "0123456789ABCDEF";
    for (int i = bytes - 1; i >= 0; i--) {
        char text[3] = { digits[(value >> (i * 8 + 4)) & 0xF],
                         digits[(value >> (i * 8)) & 0xF], '\0' };
        os->shell.term_print(text);
    }
}

int main_app(os_api_t* os) {
    net_diagnostics_t d;
    if (!os) return -1;
    os->network.net_poll();
    os->network.net_get_diagnostics(&d);
    os->shell.term_print("Network driver diagnostics\n  IO base: 0x");
    print_hex(os, d.io_base, 2);
    os->shell.term_print("\n  State: ");
    os->shell.term_print(os->network.net_is_ready() ? "ready\n" : "not ready\n");
    os->shell.term_print("  Init failure: ");
    print_u32(os, d.init_failure);
    os->shell.term_print("\n  TX attempts/submitted/busy: ");
    print_u32(os, d.tx_attempts);
    os->shell.term_print("/");
    print_u32(os, d.tx_submitted);
    os->shell.term_print("/");
    print_u32(os, d.tx_busy);
    os->shell.term_print("\n  TX completed/errors: ");
    print_u32(os, d.tx_completed);
    os->shell.term_print("/");
    print_u32(os, d.tx_errors);
    os->shell.term_print("\n  RX frames/errors/invalid: ");
    print_u32(os, d.rx_frames);
    os->shell.term_print("/");
    print_u32(os, d.rx_errors);
    os->shell.term_print("/");
    print_u32(os, d.rx_invalid);
    os->shell.term_print("\n  CSR0: 0x");
    print_hex(os, d.csr0, 2);
    os->shell.term_print("  RX descriptor status/length: 0x");
    print_hex(os, d.rx_descriptor_status, 2);
    os->shell.term_print("/");
    print_u32(os, d.rx_message_length);
    os->shell.term_print("\n  Last RX length/error CSR0: ");
    print_u32(os, d.last_rx_length);
    os->shell.term_print("/0x");
    print_hex(os, d.last_error_status, 2);
    os->shell.term_print("\n");
    return 0;
}

#include "../../system/user/sys_api.h"

static void print(const char* text) {
    uint32_t length = 0;
    while (text[length]) length++;
    (void)sys_write(text, length);
}

static void print_u32(uint32_t value) {
    char digits[11];
    int count = 0;
    if (!value) {
        print("0");
        return;
    }
    while (value) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (count) {
        char digit[2] = { digits[--count], '\0' };
        print(digit);
    }
}

static void print_hex(uint32_t value, int bytes) {
    const char* digits = "0123456789ABCDEF";
    for (int i = bytes - 1; i >= 0; i--) {
        char text[3] = { digits[(value >> (i * 8 + 4)) & 0xF],
                         digits[(value >> (i * 8)) & 0xF], '\0' };
        print(text);
    }
}

void user_main(void) {
    net_diagnostics_t d;
    int32_t ready = sys_net_diagnostics(&d);
    if (ready < 0) {
        print("Network diagnostics unavailable\n");
        sys_exit(1);
    }
    print("Network driver diagnostics\n  IO base: 0x");
    print_hex(d.io_base, 2);
    print("\n  State: ");
    print(ready ? "ready\n" : "not ready\n");
    print("  Init failure: ");
    print_u32(d.init_failure);
    print("\n  TX attempts/submitted/busy: ");
    print_u32(d.tx_attempts); print("/"); print_u32(d.tx_submitted);
    print("/"); print_u32(d.tx_busy);
    print("\n  TX completed/errors: ");
    print_u32(d.tx_completed); print("/"); print_u32(d.tx_errors);
    print("\n  RX frames/errors/invalid: ");
    print_u32(d.rx_frames); print("/"); print_u32(d.rx_errors);
    print("/"); print_u32(d.rx_invalid);
    print("\n  CSR0: 0x");
    print_hex(d.csr0, 2);
    print("  RX descriptor status/length: 0x");
    print_hex(d.rx_descriptor_status, 2); print("/");
    print_u32(d.rx_message_length);
    print("\n  Last RX length/error CSR0: ");
    print_u32(d.last_rx_length); print("/0x");
    print_hex(d.last_error_status, 2);
    print("\n");
}

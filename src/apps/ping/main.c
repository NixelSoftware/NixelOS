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

static void print_ping_time(uint32_t milliseconds) {
    if (milliseconds == 0) print("<10");
    else print_u32(milliseconds);
}

static int parse_ipv4(const char* text, uint32_t* address) {
    uint32_t result = 0;
    int parts = 0;
    while (*text && parts < 4) {
        uint32_t value = 0;
        int digits = 0;
        while (*text >= '0' && *text <= '9') {
            value = value * 10 + (uint32_t)(*text++ - '0');
            if (value > 255) return 0;
            digits++;
        }
        if (!digits) return 0;
        result = (result << 8) | value;
        parts++;
        if (parts < 4) {
            if (*text != '.') return 0;
            text++;
        }
    }
    if (parts != 4 || *text) return 0;
    *address = result;
    return 1;
}

void user_main(const char* argument) {
    char host[128];
    int host_length = 0;
    uint32_t target_ip;
    uint32_t received = 0, total = 0, minimum = 0xFFFFFFFF, maximum = 0;

    while (*argument == ' ') argument++;
    if (!*argument) {
        print("Usage: ping <host or IPv4 address>\n");
        sys_exit(1);
    }
    if (argument[0] == 'h' && argument[1] == 't' && argument[2] == 't' &&
        argument[3] == 'p' && argument[4] == ':' && argument[5] == '/' &&
        argument[6] == '/') argument += 7;
    while (*argument && *argument != '/' && *argument != ' ' &&
           host_length < (int)sizeof(host) - 1) {
        host[host_length++] = *argument++;
    }
    host[host_length] = '\0';
    if (!parse_ipv4(host, &target_ip) &&
        sys_dns_resolve(host, &target_ip) != 1) {
        print("ping: DNS lookup failed\n");
        sys_exit(1);
    }

    print("PING ");
    print(host);
    print(" (4 packets)\n");

    for (uint16_t sequence = 1; sequence <= 4; sequence++) {
        int32_t handle = sys_ping_start(target_ip, sequence);
        int32_t status = handle < 0 ? -1 : 0;
        uint32_t milliseconds = 0;
        while (status == 0) {
            status = sys_ping_poll((uint32_t)handle, &milliseconds);
            if (status == 0) sys_yield();
        }
        if (status == 1) {
            print("reply ");
            print_u32(sequence);
            print(": ");
            print_ping_time(milliseconds);
            print(" ms\n");
            received++;
            total += milliseconds;
            if (milliseconds < minimum) minimum = milliseconds;
            if (milliseconds > maximum) maximum = milliseconds;
        } else {
            print("timeout ");
            print_u32(sequence);
            print("\n");
        }
    }

    print("--- ping statistics ---\n");
    print_u32(4 - received);
    print(" packets lost (");
    print_u32((4 - received) * 25);
    print("%)\n");
    if (received) {
        print("min/avg/max = ");
        print_ping_time(minimum);
        print("/");
        print_ping_time(total / received);
        print("/");
        print_ping_time(maximum);
        print(" ms\n");
    }
}

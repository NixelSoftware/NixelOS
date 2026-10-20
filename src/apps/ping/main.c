#include "../../kernel/types.h"
#include "../../kernel/os_api.h"

static os_api_t* os;
static uint32_t target_ip;
static uint16_t target_sequence;
static uint32_t reply_tick;
static uint8_t reply_received;

#define PING_POLL_BUDGET 100000U

static void print_u32(uint32_t value) {
    char digits[11];
    int count = 0;
    if (value == 0) {
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

static void print_ping_time(uint32_t milliseconds) {
    if (milliseconds == 0) os->shell.term_print("<10");
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

static void ping_reply(uint32_t source_ip, uint16_t identifier,
                       uint16_t sequence, const uint8_t* payload,
                       uint16_t length) {
    (void)identifier;
    (void)payload;
    (void)length;
    if (source_ip == target_ip && sequence == target_sequence) {
        reply_tick = os->network.get_ticks();
        reply_received = 1;
    }
}

int main_app(os_api_t* api) {
    const char* argument;
    char host[128];
    int host_length = 0;
    uint32_t received = 0, total = 0, minimum = 0xFFFFFFFF, maximum = 0;
    const uint8_t payload[] = "operating-system";

    if (!api) return -1;
    os = api;
    argument = os->shell.command_args();
    while (*argument == ' ') argument++;
    if (*argument == '\0') {
        os->shell.term_print("Usage: ping <host or IPv4 address>\n");
        return -1;
    }
    if (argument[0] == 'h' && argument[1] == 't' && argument[2] == 't' &&
        argument[3] == 'p' && argument[4] == ':' && argument[5] == '/' &&
        argument[6] == '/') argument += 7;
    while (*argument && *argument != '/' && *argument != ' ' &&
           host_length < (int)sizeof(host) - 1) {
        host[host_length++] = *argument++;
    }
    host[host_length] = '\0';
    if (!parse_ipv4(host, &target_ip)) {
        if (!os->network.dns_resolve || !os->network.dns_resolve(host, &target_ip)) {
            os->shell.term_print("ping: DNS lookup failed\n");
            return -1;
        }
    }

    os->network.icmp_set_echo_reply_handler(ping_reply);
    os->shell.term_print("PING ");
    os->shell.term_print(host);
    os->shell.term_print(" (4 packets)\n");

    for (target_sequence = 1; target_sequence <= 4; target_sequence++) {
        uint32_t start = os->network.get_ticks();
        uint32_t deadline = start + 200;
        uint32_t poll_count = 0;
        uint8_t sent = 0;
        reply_received = 0;
        while (!sent && os->network.get_ticks() < deadline &&
               poll_count++ < PING_POLL_BUDGET) {
            os->network.net_poll();
            sent = os->network.icmp_request_echo(target_ip, 0x4F53, target_sequence,
                                         payload, sizeof(payload) - 1);
        }
        poll_count = 0;
        while (sent && !reply_received && os->network.get_ticks() < deadline &&
               poll_count++ < PING_POLL_BUDGET) {
            os->network.net_poll();
        }
        if (reply_received) {
            uint32_t milliseconds = (reply_tick - start) * 10;
            os->shell.term_print("reply ");
            print_u32(target_sequence);
            os->shell.term_print(": ");
            print_ping_time(milliseconds);
            os->shell.term_print(" ms\n");
            received++;
            total += milliseconds;
            if (milliseconds < minimum) minimum = milliseconds;
            if (milliseconds > maximum) maximum = milliseconds;
        } else {
            os->shell.term_print("timeout ");
            print_u32(target_sequence);
            os->shell.term_print("\n");
        }
    }
    os->shell.term_print("--- ping statistics ---\n");
    print_u32(4 - received);
    os->shell.term_print(" packets lost (");
    print_u32((4 - received) * 25);
    os->shell.term_print("%)\n");
    if (received) {
        os->shell.term_print("min/avg/max = ");
        print_ping_time(minimum);
        os->shell.term_print("/");
        print_ping_time(total / received);
        os->shell.term_print("/");
        print_ping_time(maximum);
        os->shell.term_print(" ms\n");
    }
    return 0;
}

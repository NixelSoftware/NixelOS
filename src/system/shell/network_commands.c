#include "network_commands.h"

static os_api_t* network_api;
static shell_output_fn command_output;
static volatile uint8_t ping_reply_received;
static volatile uint32_t ping_reply_time;
static uint32_t ping_target_ip;
static uint32_t ping_start_time;
static uint16_t ping_sequence;

static void print(const char* text) {
    if (command_output) command_output(text);
}

static void print_u32(uint32_t value) {
    char digits[11];
    int count = 0;
    if (value == 0) {
        print("0");
        return;
    }
    while (value != 0) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (count > 0) {
        char digit[2] = { digits[--count], '\0' };
        print(digit);
    }
}

static void print_ip(uint32_t address) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        print_u32((address >> shift) & 0xFF);
        if (shift != 0) print(".");
    }
}

static void print_hex_byte(uint8_t value) {
    const char* digits = "0123456789ABCDEF";
    char text[3] = { digits[value >> 4], digits[value & 0x0F], '\0' };
    print(text);
}

static int has_prefix(const char* text, const char* prefix) {
    while (*prefix) {
        if (*text++ != *prefix++) return 0;
    }
    return 1;
}

static uint8_t tick_before(uint32_t value, uint32_t deadline) {
    return (int32_t)(value - deadline) < 0;
}

static int parse_ipv4(const char* text, uint32_t* address) {
    uint32_t result = 0;
    int parts = 0;
    while (*text != '\0' && parts < 4) {
        uint32_t value = 0;
        int digits = 0;
        while (*text >= '0' && *text <= '9') {
            value = value * 10 + (uint32_t)(*text - '0');
            if (value > 255) return 0;
            text++;
            digits++;
        }
        if (digits == 0) return 0;
        result = (result << 8) | value;
        parts++;
        if (parts < 4) {
            if (*text != '.') return 0;
            text++;
        }
    }
    if (parts != 4 || *text != '\0') return 0;
    *address = result;
    return 1;
}

static void ping_reply(uint32_t source_ip, uint16_t identifier,
                       uint16_t sequence, const uint8_t* payload,
                       uint16_t length) {
    (void)identifier;
    (void)payload;
    (void)length;
    if (network_api && network_api->network.get_ticks
        && source_ip == ping_target_ip && sequence == ping_sequence) {
        ping_reply_time = network_api->network.get_ticks() - ping_start_time;
        ping_reply_received = 1;
    }
}

void shell_network_initialize(os_api_t* api) {
    network_api = api;
    if (network_api && network_api->network.icmp_set_echo_reply_handler)
        network_api->network.icmp_set_echo_reply_handler(ping_reply);
}

void shell_network_ifconfig(shell_output_fn output) {
    if (!network_api) return;
    command_output = output;

    uint64_t mac = network_api->network.get_mac_address
        ? network_api->network.get_mac_address() : 0;
    uint32_t subnet_mask = 0xFFFFFF00;
    uint32_t gateway = 0x0A000002;
    uint8_t dhcp_leased = 0;
    if (network_api->network.get_configuration)
        network_api->network.get_configuration(&subnet_mask, &gateway, &dhcp_leased);

    print("eth0\n  Link: ");
    print(network_api->network.net_is_ready && network_api->network.net_is_ready()
        ? "up\n" : "down\n");
    print("  MAC: ");
    for (int i = 5; i >= 0; i--) {
        print_hex_byte((uint8_t)(mac >> (i * 8)));
        if (i != 0) print(":");
    }
    print("\n  IP: ");
    print_ip(network_api->network.ipv4_get_local_ip
        ? network_api->network.ipv4_get_local_ip() : 0);
    print("\n  Netmask: ");
    print_ip(subnet_mask);
    print("\n  Gateway: ");
    print_ip(gateway);
    print("\n  DHCP: ");
    print(dhcp_leased ? "leased\n" : "not available\n");
}

void shell_network_netdiag(shell_output_fn output) {
    if (!network_api || !network_api->network.net_get_diagnostics) return;
    command_output = output;
    net_diagnostics_t diagnostics = {0};
    if (network_api->network.net_poll) network_api->network.net_poll();
    network_api->network.net_get_diagnostics(&diagnostics);

    print("Network driver diagnostics\n  IO base: 0x");
    print_hex_byte((uint8_t)(diagnostics.io_base >> 8));
    print_hex_byte((uint8_t)diagnostics.io_base);
    print("\n  State: ");
    print(network_api->network.net_is_ready && network_api->network.net_is_ready()
        ? "ready\n" : "not ready\n");
    print("  Init failure: "); print_u32(diagnostics.init_failure);
    print("\n  Probe SW/DW CSR0: 0x");
    print_hex_byte((uint8_t)(diagnostics.sw_csr0 >> 8));
    print_hex_byte((uint8_t)diagnostics.sw_csr0); print("/0x");
    print_hex_byte((uint8_t)(diagnostics.dw_csr0 >> 24));
    print_hex_byte((uint8_t)(diagnostics.dw_csr0 >> 16));
    print_hex_byte((uint8_t)(diagnostics.dw_csr0 >> 8));
    print_hex_byte((uint8_t)diagnostics.dw_csr0); print("  CSR0: 0x");
    print_hex_byte((uint8_t)(diagnostics.csr0 >> 8));
    print_hex_byte((uint8_t)diagnostics.csr0);
    print("\n  TX attempts/submitted/busy: "); print_u32(diagnostics.tx_attempts);
    print("/"); print_u32(diagnostics.tx_submitted); print("/");
    print_u32(diagnostics.tx_busy); print("\n  TX completed/errors: ");
    print_u32(diagnostics.tx_completed); print("/"); print_u32(diagnostics.tx_errors);
    print("\n  Last TX status/misc: 0x");
    print_hex_byte((uint8_t)(diagnostics.last_tx_status >> 8));
    print_hex_byte((uint8_t)diagnostics.last_tx_status); print("/0x");
    print_hex_byte((uint8_t)(diagnostics.last_tx_misc >> 24));
    print_hex_byte((uint8_t)(diagnostics.last_tx_misc >> 16));
    print_hex_byte((uint8_t)(diagnostics.last_tx_misc >> 8));
    print_hex_byte((uint8_t)diagnostics.last_tx_misc);
    print("\n  TX descriptor address/length/status: 0x");
    print_hex_byte((uint8_t)(diagnostics.tx_descriptor_address >> 24));
    print_hex_byte((uint8_t)(diagnostics.tx_descriptor_address >> 16));
    print_hex_byte((uint8_t)(diagnostics.tx_descriptor_address >> 8));
    print_hex_byte((uint8_t)diagnostics.tx_descriptor_address); print("/");
    print_u32((uint32_t)(uint16_t)diagnostics.tx_descriptor_length); print("/0x");
    print_hex_byte((uint8_t)(diagnostics.tx_descriptor_status >> 8));
    print_hex_byte((uint8_t)diagnostics.tx_descriptor_status);
    print("\n  RX frames/errors/invalid: "); print_u32(diagnostics.rx_frames);
    print("/"); print_u32(diagnostics.rx_errors); print("/");
    print_u32(diagnostics.rx_invalid); print("\n  RX descriptor status/length: 0x");
    print_hex_byte((uint8_t)(diagnostics.rx_descriptor_status >> 8));
    print_hex_byte((uint8_t)diagnostics.rx_descriptor_status); print("/");
    print_u32(diagnostics.rx_message_length); print("\n  Last RX length: ");
    print_u32(diagnostics.last_rx_length); print("\n  Last error CSR0: 0x");
    print_hex_byte((uint8_t)(diagnostics.last_error_status >> 8));
    print_hex_byte((uint8_t)diagnostics.last_error_status);
    print("\n  Ring indexes TX/RX: "); print_u32(diagnostics.tx_index);
    print("/"); print_u32(diagnostics.rx_index); print("\n");
}

void shell_network_ping(const char* argument, shell_output_fn output) {
    if (!network_api || !argument) return;
    command_output = output;

    uint32_t address;
    uint32_t received = 0;
    uint32_t total = 0;
    uint32_t minimum = 0xFFFFFFFF;
    uint32_t maximum = 0;
    char host[32];
    int host_length = 0;
    const uint8_t payload[] = "operating-system";

    if (has_prefix(argument, "http://")) argument += 7;
    else if (has_prefix(argument, "https://")) argument += 8;
    while (argument[host_length] != '\0' && argument[host_length] != '/'
           && argument[host_length] != ' ' && host_length < (int)sizeof(host) - 1) {
        host[host_length] = argument[host_length];
        host_length++;
    }
    host[host_length] = '\0';

    if (!parse_ipv4(host, &address)) {
        print("Usage: ping <IPv4 address>\n");
        print("URL paths are accepted for IPv4 URLs; DNS is not supported.\n");
        return;
    }

    print("PING "); print(host); print(" (4 packets)\n");
    ping_target_ip = address;
    for (ping_sequence = 1; ping_sequence <= 4; ping_sequence++) {
        uint32_t start = network_api->network.get_ticks
            ? network_api->network.get_ticks() : 0;
        uint32_t deadline = start + 200;
        uint32_t next_attempt = start;
        uint8_t sent = 0;
        ping_reply_received = 0;
        ping_start_time = start;

        while (!sent && network_api->network.get_ticks
               && tick_before(network_api->network.get_ticks(), deadline)) {
            if (network_api->network.net_poll) network_api->network.net_poll();
            uint32_t now = network_api->network.get_ticks();
            if (!tick_before(now, next_attempt) && network_api->network.icmp_request_echo) {
                sent = network_api->network.icmp_request_echo(address, 0x4F53,
                    ping_sequence, payload, sizeof(payload) - 1);
                next_attempt = now + 10;
            }
            if (!sent) for (volatile uint32_t delay = 0; delay < 2000; delay++)
                __asm__ volatile ("pause");
        }

        while (sent && !ping_reply_received && network_api->network.get_ticks
               && tick_before(network_api->network.get_ticks(), deadline)) {
            if (network_api->network.net_poll) network_api->network.net_poll();
            for (volatile uint32_t delay = 0; delay < 2000; delay++)
                __asm__ volatile ("pause");
        }

        if (ping_reply_received) {
            uint32_t milliseconds = ping_reply_time * 10;
            print("reply "); print_u32(ping_sequence); print(": ");
            print_u32(milliseconds); print(" ms\n");
            received++; total += milliseconds;
            if (milliseconds < minimum) minimum = milliseconds;
            if (milliseconds > maximum) maximum = milliseconds;
        } else {
            print("timeout "); print_u32(ping_sequence); print("\n");
        }
    }

    print("--- ping statistics ---\n");
    print_u32(4 - received); print(" packets lost (");
    print_u32((4 - received) * 25); print("%)\n");
    if (received) {
        print("min/avg/max = "); print_u32(minimum); print("/");
        print_u32(total / received); print("/"); print_u32(maximum); print(" ms\n");
    }
}

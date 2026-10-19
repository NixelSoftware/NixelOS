#include "../../kernel/types.h"
#include "../../kernel/os_api.h"

#define TCP_PROTOCOL 6
#define HTTP_PORT 80
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_PSH 0x08
#define TCP_ACK 0x10
#define MAX_RESPONSE 65536

static os_api_t* os;
static volatile uint8_t connected;
static volatile uint8_t response_done;
static uint32_t server_ip;
static uint16_t local_port = 41000;
static uint32_t local_seq;
static uint32_t remote_seq;
static uint8_t response[MAX_RESPONSE];
static uint32_t response_len;

static uint16_t checksum(const uint8_t* data, uint32_t len) {
    uint32_t sum = 0;
    while (len >= 2) {
        sum += ((uint16_t)data[0] << 8) | data[1];
        data += 2;
        len -= 2;
    }
    if (len) sum += (uint16_t)data[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint8_t send_segment(uint8_t flags, const uint8_t* payload, uint16_t length) {
    uint8_t packet[512];
    uint8_t pseudo[12 + 20 + 256];
    uint16_t tcp_length = 20 + length;

    if (length > 256) return 0;
    packet[0] = (uint8_t)(local_port >> 8);
    packet[1] = (uint8_t)local_port;
    packet[2] = 0;
    packet[3] = HTTP_PORT;
    packet[4] = (uint8_t)(local_seq >> 24);
    packet[5] = (uint8_t)(local_seq >> 16);
    packet[6] = (uint8_t)(local_seq >> 8);
    packet[7] = (uint8_t)local_seq;
    packet[8] = (uint8_t)(remote_seq >> 24);
    packet[9] = (uint8_t)(remote_seq >> 16);
    packet[10] = (uint8_t)(remote_seq >> 8);
    packet[11] = (uint8_t)remote_seq;
    packet[12] = 0x50;
    packet[13] = flags;
    packet[14] = 0x20;
    packet[15] = 0;
    packet[16] = packet[17] = packet[18] = packet[19] = 0;
    for (uint16_t i = 0; i < length; i++) packet[20 + i] = payload[i];

    uint32_t local_ip = os->ipv4_get_local_ip();
    pseudo[0] = (uint8_t)(local_ip >> 24);
    pseudo[1] = (uint8_t)(local_ip >> 16);
    pseudo[2] = (uint8_t)(local_ip >> 8);
    pseudo[3] = (uint8_t)local_ip;
    pseudo[4] = (uint8_t)(server_ip >> 24);
    pseudo[5] = (uint8_t)(server_ip >> 16);
    pseudo[6] = (uint8_t)(server_ip >> 8);
    pseudo[7] = (uint8_t)server_ip;
    pseudo[8] = 0;
    pseudo[9] = TCP_PROTOCOL;
    pseudo[10] = (uint8_t)(tcp_length >> 8);
    pseudo[11] = (uint8_t)tcp_length;
    for (uint16_t i = 0; i < tcp_length; i++) pseudo[12 + i] = packet[i];

    uint16_t sum = checksum(pseudo, 12 + tcp_length);
    packet[16] = (uint8_t)(sum >> 8);
    packet[17] = (uint8_t)sum;
    if (!os->ipv4_send(server_ip, TCP_PROTOCOL, packet, tcp_length)) return 0;

    if (flags & (TCP_SYN | TCP_FIN)) local_seq++;
    else local_seq += length;
    return 1;
}

static uint8_t tcp_handler(uint32_t source, uint32_t destination,
                           const uint8_t* packet, uint32_t length) {
    (void)source;
    (void)destination;
    if (length < 20) return 0;
    uint16_t destination_port = ((uint16_t)packet[2] << 8) | packet[3];
    if (destination_port != local_port) return 0;

    uint32_t sequence = ((uint32_t)packet[4] << 24) |
                        ((uint32_t)packet[5] << 16) |
                        ((uint32_t)packet[6] << 8) | packet[7];
    uint8_t flags = packet[13];
    uint16_t header_length = ((packet[12] >> 4) & 0x0F) * 4;
    if (header_length > length) return 0;
    uint32_t data_length = length - header_length;

    if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
        remote_seq = sequence + 1;
        connected = 1;
        send_segment(TCP_ACK, 0, 0);
        return 1;
    }
    if (data_length) {
        remote_seq = sequence + data_length;
        for (uint32_t i = 0; i < data_length && response_len < MAX_RESPONSE; i++) {
            response[response_len++] = packet[header_length + i];
        }
        send_segment(TCP_ACK, 0, 0);
        return 1;
    }
    if (flags & TCP_FIN) {
        remote_seq = sequence + 1;
        send_segment(TCP_ACK, 0, 0);
        response_done = 1;
        return 1;
    }
    return 0;
}

static int text_equal(const char* left, const char* right) {
    while (*left && *left == *right) {
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static void print_number(uint32_t value) {
    char digits[11];
    int count = 0;
    if (!value) {
        os->term_print("0");
        return;
    }
    while (value) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (count) {
        char digit[2] = { digits[--count], '\0' };
        os->term_print(digit);
    }
}

static int parse_url(const char* url, char* host, char* path) {
    const char* start = url;
    if (url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p' && url[4] == ':' && url[5] == '/' && url[6] == '/') {
        start = url + 7;
    }
    int host_len = 0;
    while (start[host_len] && start[host_len] != '/' && host_len < 127) {
        host[host_len] = start[host_len];
        host_len++;
    }
    host[host_len] = '\0';
    if (!host_len) return 0;
    int path_len = 0;
    const char* path_start = start + host_len;
    if (*path_start == '/') {
        while (path_start[path_len] && path_len < 255) {
            path[path_len] = path_start[path_len];
            path_len++;
        }
    } else {
        path[path_len++] = '/';
    }
    path[path_len] = '\0';
    return 1;
}

static int run_request(const char* host, const char* path) {
    local_port++;
    local_seq = 0x12000000 + local_port;
    remote_seq = 0;
    response_len = 0;
    response_done = 0;
    connected = 0;

    for (int attempt = 0; attempt < 3 && !connected; attempt++) {
        send_segment(TCP_SYN, 0, 0);
        for (uint32_t poll = 0; poll < 100000 && !connected; poll++) {
            os->net_poll();
            for (volatile int delay = 0; delay < 50; delay++) __asm__ volatile ("pause");
        }
    }
    if (!connected) return 0;

    char request[256];
    int length = 0;
    const char* prefix = "GET ";
    while (*prefix) request[length++] = *prefix++;
    for (int i = 0; path[i] && length < 200; i++) request[length++] = path[i];
    const char* middle = " HTTP/1.1\r\nHost: ";
    while (*middle && length < 255) request[length++] = *middle++;
    for (int i = 0; host[i] && length < 255; i++) request[length++] = host[i];
    const char* suffix = "\r\nUser-Agent: curl/0.1 (NixelOS)\r\nConnection: close\r\n\r\n";
    while (*suffix && length < 255) request[length++] = *suffix++;
    send_segment(TCP_PSH | TCP_ACK, (const uint8_t*)request, (uint16_t)length);

    for (uint32_t poll = 0; poll < 400000 && !response_done; poll++) {
        os->net_poll();
        for (volatile int delay = 0; delay < 50; delay++) __asm__ volatile ("pause");
    }
    return response_len != 0;
}

int main_app(os_api_t* api) {
    if (!api) return -1;
    os = api;
    const char* args = os->command_args();
    char host[128];
    char path[256];
    char output_file[64];
    output_file[0] = '\0';

    while (*args == ' ') args++;
    if (!*args || text_equal(args, "--help")) {
        os->term_print("Usage: curl [-o file] http://host/path\n");
        return 0;
    }
    if (args[0] == '-' && args[1] == 'o' && args[2] == ' ') {
        args += 3;
        int i = 0;
        while (args[i] && args[i] != ' ' && i < 63) { output_file[i] = args[i]; i++; }
        output_file[i] = '\0';
        while (args[i] == ' ') i++;
        args += i;
    }
    if (!parse_url(args, host, path)) {
        os->term_print("curl: invalid URL\n");
        return -1;
    }

    if (!os->dns_resolve(host, &server_ip)) {
        os->term_print("curl: DNS lookup failed\n");
        return -1;
    }
    os->ipv4_set_handler(TCP_PROTOCOL, tcp_handler);
    if (!run_request(host, path)) {
        os->term_print("curl: request failed\n");
        os->ipv4_set_handler(TCP_PROTOCOL, 0);
        return -1;
    }

    uint32_t body_start = 0;
    for (uint32_t i = 0; i + 3 < response_len; i++) {
        if (response[i] == '\r' && response[i + 1] == '\n' &&
            response[i + 2] == '\r' && response[i + 3] == '\n') {
            body_start = i + 4;
            break;
        }
    }
    if (!body_start) body_start = response_len;
    if (output_file[0]) {
        int written = os->fs_write(output_file, response + body_start,
                                   response_len - body_start);
        os->term_print(written >= 0 ? "curl: saved " : "curl: save failed\n");
        if (written >= 0) { print_number(response_len - body_start); os->term_print(" bytes\n"); }
    } else {
        for (uint32_t i = 0; i < response_len; i++) {
            char text[2] = { (char)response[i], '\0' };
            os->term_print(text);
        }
    }
    os->ipv4_set_handler(TCP_PROTOCOL, 0);
    return 0;
}
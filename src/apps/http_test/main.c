#include "../../kernel/types.h"
#include "../../kernel/os_api.h"

static os_api_t* os;

#define TCP_PROTOCOL   6
#define TCP_PORT_HTTP  80

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

typedef enum {
    APP_TCP_CLOSED,
    APP_TCP_SYN_SENT,
    APP_TCP_ESTABLISHED,
    APP_TCP_FIN_WAIT
} app_tcp_state_t;

static volatile app_tcp_state_t tcp_state = APP_TCP_CLOSED;
static uint32_t local_seq = 0x10002000;
static uint32_t remote_seq = 0;
static uint16_t client_port = 49152;
static uint32_t server_ip = 0;

static char http_resp[1024];
static uint16_t http_resp_len = 0;
static volatile uint8_t got_response = 0;

static uint16_t calc_checksum(const void* data, uint32_t len) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t sum = 0;
    while (len >= 2) {
        sum += ((uint16_t)bytes[0] << 8) | bytes[1];
        bytes += 2;
        len -= 2;
    }
    if (len > 0) {
        sum += (uint16_t)bytes[0] << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

static uint8_t send_tcp_segment(uint8_t flags, const uint8_t* payload, uint16_t payload_len) {
    uint8_t packet[512];
    uint8_t pseudo[12 + 20 + 256];
    uint16_t tcp_len = 20 + payload_len;

    packet[0] = (uint8_t)(client_port >> 8);
    packet[1] = (uint8_t)(client_port & 0xFF);
    packet[2] = 0x00;
    packet[3] = (uint8_t)TCP_PORT_HTTP;

    packet[4] = (uint8_t)(local_seq >> 24);
    packet[5] = (uint8_t)(local_seq >> 16);
    packet[6] = (uint8_t)(local_seq >> 8);
    packet[7] = (uint8_t)(local_seq & 0xFF);

    packet[8]  = (uint8_t)(remote_seq >> 24);
    packet[9]  = (uint8_t)(remote_seq >> 16);
    packet[10] = (uint8_t)(remote_seq >> 8);
    packet[11] = (uint8_t)(remote_seq & 0xFF);

    packet[12] = 0x50; 
    packet[13] = flags;

    packet[14] = 0x10; 
    packet[15] = 0x00;

    packet[16] = 0;
    packet[17] = 0;
    packet[18] = 0;
    packet[19] = 0;

    for (uint16_t i = 0; i < payload_len; i++) {
        packet[20 + i] = payload[i];
    }

    uint32_t local_ip = os->ipv4_get_local_ip();
    pseudo[0] = (uint8_t)(local_ip >> 24);
    pseudo[1] = (uint8_t)(local_ip >> 16);
    pseudo[2] = (uint8_t)(local_ip >> 8);
    pseudo[3] = (uint8_t)(local_ip & 0xFF);

    pseudo[4] = (uint8_t)(server_ip >> 24);
    pseudo[5] = (uint8_t)(server_ip >> 16);
    pseudo[6] = (uint8_t)(server_ip >> 8);
    pseudo[7] = (uint8_t)(server_ip & 0xFF);

    pseudo[8] = 0x00;
    pseudo[9] = TCP_PROTOCOL;
    pseudo[10] = (uint8_t)(tcp_len >> 8);
    pseudo[11] = (uint8_t)(tcp_len & 0xFF);

    for (uint16_t i = 0; i < tcp_len; i++) {
        pseudo[12 + i] = packet[i];
    }

    uint16_t csum = calc_checksum(pseudo, 12 + tcp_len);
    packet[16] = (uint8_t)(csum >> 8);
    packet[17] = (uint8_t)(csum & 0xFF);

    uint8_t ok = os->ipv4_send(server_ip, TCP_PROTOCOL, packet, tcp_len);
    if (ok) {
        if (flags & (TCP_SYN | TCP_FIN)) local_seq++;
        else local_seq += payload_len;
    }
    return ok;
}

static uint8_t app_tcp_incoming(uint32_t src_ip, uint32_t dst_ip, const uint8_t* payload, uint32_t len) {
    (void)dst_ip;
    if (len < 20) return 0;

    uint16_t dst_port = ((uint16_t)payload[2] << 8) | payload[3];
    if (dst_port != client_port) return 0;

    uint32_t seq = ((uint32_t)payload[4] << 24) | ((uint32_t)payload[5] << 16) |
                   ((uint32_t)payload[6] << 8)  | (uint32_t)payload[7];

    uint8_t flags = payload[13];
    uint16_t hlen = ((payload[12] >> 4) & 0x0F) * 4;
    uint16_t data_len = len - hlen;

    
    if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
        remote_seq = seq + 1;
        tcp_state = APP_TCP_ESTABLISHED;
        send_tcp_segment(TCP_ACK, 0, 0);
        return 1;
    }

    
    if (data_len > 0) {
        remote_seq = seq + data_len;
        for (int i = 0; i < data_len && http_resp_len < sizeof(http_resp) - 1; i++) {
            http_resp[http_resp_len++] = (char)payload[hlen + i];
        }
        http_resp[http_resp_len] = '\0';
        got_response = 1;
        send_tcp_segment(TCP_ACK, 0, 0);
        return 1;
    }

    if (flags & TCP_FIN) {
        remote_seq = seq + 1;
        send_tcp_segment(TCP_ACK, 0, 0);
        tcp_state = APP_TCP_CLOSED;
        return 1;
    }

    return 0;
}

int main_app(os_api_t* api) {
    if (!api) return -1;
    os = api;

    
    server_ip = (104U << 24) | (254U << 16) | (42U << 8) | 159U;
    client_port++;
    if (client_port < 40000) client_port = 49152;

    os->term_print("Hooking TCP...\n");
    os->ipv4_set_handler(TCP_PROTOCOL, app_tcp_incoming);

    tcp_state = APP_TCP_SYN_SENT;
    local_seq = 0x54321;
    http_resp_len = 0;
    got_response = 0;

    os->term_print("Sending SYN...\n");

    
    int connected = 0;
    for (int attempt = 0; attempt < 5 && !connected; attempt++) {
        send_tcp_segment(TCP_SYN, 0, 0);

        for (uint32_t poll = 0; poll < 100000; poll++) {
            os->net_poll();
            if (tcp_state == APP_TCP_ESTABLISHED) {
                connected = 1;
                break;
            }
            for (volatile int d = 0; d < 100; d++) __asm__ volatile ("pause");
        }
        if (!connected) os->term_print(".\n");
    }

    if (!connected) {
        os->term_print("Error: Handshake timed out.\n");
        os->ipv4_set_handler(TCP_PROTOCOL, 0);
        return -1;
    }

    os->term_print("Connected! Sending HTTP GET...\n");
    const char get_req[] =
        "GET / HTTP/1.1\r\n"
        "Host: connectiontest.menifeetech.pp.ua\r\n"
        "User-Agent: VixelOS-Connection-Test\r\n"
        "Connection: close\r\n\r\n";

    send_tcp_segment(TCP_PSH | TCP_ACK, (const uint8_t*)get_req, sizeof(get_req) - 1);

    
    for (uint32_t poll = 0; poll < 200000 && !got_response; poll++) {
        os->net_poll();
        for (volatile int d = 0; d < 100; d++) __asm__ volatile ("pause");
    }

    os->term_print("\n=== HTTP RESULT ===\n");
    if (http_resp_len == 0) {
        os->term_print("No response received.\n");
    } else {
        if (http_resp[0] == 'H' && http_resp[1] == 'T') {
            int p = 0;
            while (http_resp[p] && http_resp[p] != ' ') p++;
            if (http_resp[p] == ' ') p++;
            os->term_print("Status: ");
            char code[4] = { http_resp[p], http_resp[p+1], http_resp[p+2], '\0' };
            os->term_print(code);
            os->term_print("\n");
        }

        const char* body = 0;
        for (int i = 0; i < http_resp_len - 3; i++) {
            if (http_resp[i] == '\r' && http_resp[i+1] == '\n' &&
                http_resp[i+2] == '\r' && http_resp[i+3] == '\n') {
                body = &http_resp[i + 4];
                break;
            }
        }
        os->term_print("Body:\n");
        os->term_print(body ? body : http_resp);
        os->term_print("\n");
    }

    os->ipv4_set_handler(TCP_PROTOCOL, 0);
    return 0;
}
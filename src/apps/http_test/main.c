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

static uint8_t download_buffer[65536]; 
static uint32_t download_len = 0;
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

    packet[14] = 0x20; 
    packet[15] = 0x00;

    packet[16] = 0;
    packet[17] = 0;
    packet[18] = 0;
    packet[19] = 0;

    for (uint16_t i = 0; i < payload_len; i++) {
        packet[20 + i] = payload[i];
    }

    uint32_t local_ip = os->network.ipv4_get_local_ip();
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

    uint8_t ok = os->network.ipv4_send(server_ip, TCP_PROTOCOL, packet, tcp_len);
    if (ok) {
        if (flags & (TCP_SYN | TCP_FIN)) local_seq++;
        else local_seq += payload_len;
    }
    return ok;
}

static uint8_t app_tcp_incoming(uint32_t src_ip, uint32_t dst_ip, const uint8_t* payload, uint32_t len) {
    (void)src_ip;
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
        for (uint16_t i = 0; i < data_len && download_len < sizeof(download_buffer); i++) {
            download_buffer[download_len++] = payload[hlen + i];
        }
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

static int perform_http_transaction(const char* host, const char* path, uint8_t* out_body, uint32_t* out_body_len) {
    client_port++;
    if (client_port < 40000) client_port = 49152;

    tcp_state = APP_TCP_SYN_SENT;
    local_seq = 0x54321 + client_port;
    download_len = 0;
    got_response = 0;

    
    int connected = 0;
    for (int attempt = 0; attempt < 5 && !connected; attempt++) {
        send_tcp_segment(TCP_SYN, 0, 0);

        for (uint32_t poll = 0; poll < 100000; poll++) {
            os->network.net_poll();
            if (tcp_state == APP_TCP_ESTABLISHED) {
                connected = 1;
                break;
            }
            for (volatile int d = 0; d < 100; d++) __asm__ volatile ("pause");
        }
        if (!connected) os->shell.term_print(".");
    }

    if (!connected) {
        os->shell.term_print("\nError: Handshake failed.\n");
        return 0;
    }

    
    char req[256];
    int p = 0;
    const char* part1 = "GET ";
    while (*part1) req[p++] = *part1++;
    while (*path) req[p++] = *path++;
    const char* part2 = " HTTP/1.1\r\nHost: ";
    while (*part2) req[p++] = *part2++;
    while (*host) req[p++] = *host++;
    const char* part3 = "\r\nUser-Agent: NixelOS-Connection-Test\r\nConnection: close\r\n\r\n";
    while (*part3) req[p++] = *part3++;
    req[p] = '\0';

    send_tcp_segment(TCP_PSH | TCP_ACK, (const uint8_t*)req, p);

    
    for (uint32_t poll = 0; poll < 400000; poll++) {
        os->network.net_poll();
        if (tcp_state == APP_TCP_CLOSED && got_response) {
            break;
        }
        for (volatile int d = 0; d < 100; d++) __asm__ volatile ("pause");
    }

    if (download_len == 0) {
        return 0;
    }

    
    uint32_t body_start = 0;
    for (uint32_t i = 0; i < download_len - 3; i++) {
        if (download_buffer[i] == '\r' && download_buffer[i+1] == '\n' &&
            download_buffer[i+2] == '\r' && download_buffer[i+3] == '\n') {
            body_start = i + 4;
            break;
        }
    }

    if (body_start == 0) {
        return 0;
    }

    *out_body_len = download_len - body_start;
    for (uint32_t i = 0; i < *out_body_len; i++) {
        out_body[i] = download_buffer[body_start + i];
    }
    return 1;
}

int main_app(os_api_t* api) {
    if (!api) return -1;
    os = api;

    
    server_ip = (104U << 24) | (254U << 16) | (42U << 8) | 159U;

    os->shell.term_print("Hooking TCP...\n");
    os->network.ipv4_set_handler(TCP_PROTOCOL, app_tcp_incoming);

    
    os->shell.term_print("1. Testing connection to connectiontest.menifeetech.pp.ua...\n");
    static uint8_t body_buf[4096];
    uint32_t body_len = 0;

    if (perform_http_transaction("connectiontest.menifeetech.pp.ua", "/", body_buf, &body_len)) {
        os->shell.term_print("Connection Test OK!\nBody: ");
        body_buf[body_len < 128 ? body_len : 127] = '\0';
        os->shell.term_print((const char*)body_buf);
        os->shell.term_print("\n\n");
    } else {
        os->shell.term_print("Connection test failed.\n");
    }

    
    os->shell.term_print("2. Downloading NabilShuail95.bmp from descargar.menifeetech.pp.ua...\n");
    static uint8_t bmp_dest[65536];
    uint32_t bmp_len = 0;

    if (perform_http_transaction("descargar.menifeetech.pp.ua", "/NabilShuail95.bmp", bmp_dest, &bmp_len)) {
        os->shell.term_print("Download complete (");
        
        
        char digits[10];
        int count = 0;
        uint32_t tmp = bmp_len;
        if (tmp == 0) digits[count++] = '0';
        while (tmp > 0) { digits[count++] = (tmp % 10) + '0'; tmp /= 10; }
        for (int i = count - 1; i >= 0; i--) {
            char ch[2] = { digits[i], '\0' };
            os->shell.term_print(ch);
        }
        os->shell.term_print(" bytes).\nSaving to SFS disk as NabilShuail95.bmp...\n");

        int written = os->fs.write("NabilShuail95.bmp", bmp_dest, bmp_len);
        if (written > 0) {
            os->shell.term_print("File saved successfully! Run 'view NabilShuail95.bmp' to display it.\n");
        } else {
            os->shell.term_print("Error: Failed writing file to SFS disk.\n");
        }
    } else {
        os->shell.term_print("Error: Failed downloading NabilShuail95.bmp.\n");
    }

    os->network.ipv4_set_handler(TCP_PROTOCOL, 0);
    return 0;
}
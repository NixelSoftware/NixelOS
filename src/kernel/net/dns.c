#include "../mutex.h"
#include "dns.h"
#include "udp.h"
#include "dhcp.h"
#include "../kernel_net.h"

#define DNS_PORT 53
#define DNS_MAX_NAME 253
#define DNS_PACKET_SIZE 512
#define PRIMARY_DNS_SERVER 0x01010101
#define DNS_ASYNC_TIMEOUT_TICKS 500u
extern volatile uint32_t frame_timer;

static udp_socket_t* dns_socket;
static volatile uint8_t dns_done;
static volatile uint8_t dns_success;
static uint32_t dns_address;
static uint16_t dns_id;
static uint16_t next_dns_id = 0x4E53;
static uint8_t dns_packet[DNS_PACKET_SIZE];
static uint8_t dns_async_active;
static uint8_t dns_async_fallback_tried;
static uint32_t dns_async_server;
static uint32_t dns_async_started_tick;
static uint16_t dns_async_query_length;

static void dns_receive(udp_socket_t* socket, uint32_t source_ip,
                        uint16_t source_port, const uint8_t* data,
                        uint16_t length);

static uint8_t start_query_server(uint32_t server, uint16_t id, uint16_t length) {
    dns_packet[0] = (uint8_t)(id >> 8);
    dns_packet[1] = (uint8_t)id;
    dns_id = id;
    dns_done = 0;
    dns_success = 0;
    dns_socket = udp_connect(server, DNS_PORT);
    if (!dns_socket) return 0;
    udp_bind(dns_socket, dns_receive);
    if (udp_send(dns_socket, dns_packet, length)) return 1;
    udp_disconnect(dns_socket);
    dns_socket = 0;
    return 0;
}

static uint16_t read_be16(const uint8_t* data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t read_be32(const uint8_t* data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static int skip_name(const uint8_t* data, uint16_t length, uint16_t offset) {
    uint16_t steps = 0;
    while (offset < length && steps++ < length) {
        uint8_t size = data[offset++];
        if (size == 0) return offset;
        if ((size & 0xC0) == 0xC0) return offset + 1 < length ? offset + 1 : -1;
        if (size > 63 || (uint32_t)offset + size > length) return -1;
        offset += size;
    }
    return -1;
}

static void dns_receive(udp_socket_t* socket, uint32_t source_ip,
                        uint16_t source_port, const uint8_t* data,
                        uint16_t length) {
    (void)socket;
    if (source_ip != udp_get_remote_ip(socket) || source_port != DNS_PORT
        || length < 12 || read_be16(data) != dns_id) return;
    /* Require a response to our recursive, single-question A query. */
    if (!(read_be16(data + 2) & 0x8000)
        || (read_be16(data + 2) & 0x7800) != 0
        || read_be16(data + 4) != 1
        || (read_be16(data + 2) & 0x0200)
        || (read_be16(data + 6) == 0)) {
        dns_done = 1;
        return;
    }
    if ((read_be16(data + 2) & 0x000F) != 0) {
        dns_done = 1;
        return;
    }

    uint16_t offset = 12;
    uint16_t questions = read_be16(data + 4);
    uint16_t answers = read_be16(data + 6);
    for (uint16_t i = 0; i < questions; i++) {
        int next = skip_name(data, length, offset);
        if (next < 0 || (uint32_t)next + 4 > length) { dns_done = 1; return; }
        offset = (uint16_t)next + 4;
    }
    for (uint16_t i = 0; i < answers && (uint32_t)offset + 12 <= length; i++) {
        int next = skip_name(data, length, offset);
        if (next < 0 || (uint32_t)next + 10 > length) break;
        offset = (uint16_t)next;
        uint16_t type = read_be16(data + offset);
        uint16_t class_code = read_be16(data + offset + 2);
        uint16_t data_length = read_be16(data + offset + 8);
        offset += 10;
        if ((uint32_t)offset + data_length > length) break;
        if (type == 1 && class_code == 1 && data_length == 4) {
            dns_address = read_be32(data + offset);
            dns_success = 1;
            dns_done = 1;
            return;
        }
        offset += data_length;
    }
    dns_done = 1;
}

uint8_t dns_resolve(const char* hostname, uint32_t* address) {
    if (!address || !dns_resolve_async_start(hostname)) return 0;
    int result;
    while ((result = dns_resolve_async_poll(address)) == 0) task_sleep_ms(1);
    return result == 1;
}

uint8_t dns_resolve_async_start(const char* hostname) {
    MUTEX_GUARD(kernel_mutex);
    uint32_t dns_server;
    uint16_t offset = 13;
    uint16_t label_start = 12;
    uint16_t label_length = 0;
    uint16_t hostname_length = 0;
    uint16_t id;
    if (!hostname || !*hostname || dns_async_active) return 0;
    {
        const dhcp_lease_t* lease = dhcp_get_lease();
        dns_server = lease && lease->leased && lease->dns
                   ? lease->dns : PRIMARY_DNS_SERVER;
    }
    for (uint16_t i = 0; i <= DNS_MAX_NAME && hostname[i]; i++) {
        if (++hostname_length > DNS_MAX_NAME) return 0;
        if (hostname[i] == '.') {
            if (!label_length || label_length > 63 ||
                offset + label_length + 2 >= DNS_PACKET_SIZE) return 0;
            dns_packet[label_start] = (uint8_t)label_length;
            label_start = offset++;
            label_length = 0;
        } else {
            if ((uint8_t)hostname[i] < 0x21 || hostname[i] == 0x7F ||
                offset >= DNS_PACKET_SIZE - 1) return 0;
            dns_packet[offset++] = (uint8_t)hostname[i];
            label_length++;
        }
    }
    if (!label_length || label_length > 63 || offset >= DNS_PACKET_SIZE - 5)
        return 0;
    dns_packet[label_start] = (uint8_t)label_length;
    dns_packet[offset++] = 0;
    dns_packet[offset++] = 0;
    dns_packet[offset++] = 1;
    dns_packet[offset++] = 0;
    dns_packet[offset++] = 1;
    for (uint16_t i = 0; i < 12; i++) dns_packet[i] = 0;
    dns_packet[2] = 1;
    dns_packet[5] = 1;

    id = ++next_dns_id;
    dns_async_active = 1;
    dns_async_fallback_tried = dns_server == PRIMARY_DNS_SERVER;
    dns_async_server = dns_server;
    dns_async_started_tick = frame_timer;
    dns_async_query_length = offset;
    if (!start_query_server(dns_server, id, offset)) {
        if (dns_server == PRIMARY_DNS_SERVER) {
            dns_async_active = 0;
            return 0;
        }
        dns_async_fallback_tried = 1;
        dns_async_server = PRIMARY_DNS_SERVER;
        id = ++next_dns_id;
        if (!start_query_server(PRIMARY_DNS_SERVER, id, offset)) {
            dns_async_active = 0;
            return 0;
        }
        dns_async_started_tick = frame_timer;
    }
    return 1;
}

int dns_resolve_async_poll(uint32_t* address) {
    MUTEX_GUARD(kernel_mutex);
    if (!dns_async_active || !address) return -1;
    if (!dns_done && (uint32_t)(frame_timer - dns_async_started_tick) <
                     DNS_ASYNC_TIMEOUT_TICKS) return 0;
    if (dns_done && dns_success) {
        *address = dns_address;
        udp_disconnect(dns_socket);
        dns_socket = 0;
        dns_async_active = 0;
        return 1;
    }
    udp_disconnect(dns_socket);
    dns_socket = 0;
    if (!dns_async_fallback_tried && dns_async_server != PRIMARY_DNS_SERVER) {
        dns_async_fallback_tried = 1;
        dns_async_server = PRIMARY_DNS_SERVER;
        dns_async_started_tick = frame_timer;
        uint16_t id = ++next_dns_id;
        if (start_query_server(PRIMARY_DNS_SERVER, id, dns_async_query_length)) {
            return 0;
        }
    }
    dns_async_active = 0;
    return -1;
}

void dns_resolve_async_cancel(void) {
    MUTEX_GUARD(kernel_mutex);
    if (!dns_async_active) return;
    udp_disconnect(dns_socket);
    dns_socket = 0;
    dns_async_active = 0;
}

#include "dhcp.h"
#include "udp.h"
#include "ipv4.h"
#include "../drivers/net.h"

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DHCP_BOOTREQUEST 1
#define DHCP_BOOTREPLY 2
#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_ACK 5
#define DHCP_OPTION_SUBNET_MASK 1
#define DHCP_OPTION_ROUTER 3
#define DHCP_OPTION_DNS 6
#define DHCP_OPTION_LEASE_TIME 51
#define DHCP_OPTION_MESSAGE_TYPE 53
#define DHCP_OPTION_SERVER_ID 54
#define DHCP_OPTION_END 255
#define DHCP_MAGIC 0x63825363

static udp_socket_t* dhcp_socket;
static dhcp_lease_t current_lease;
static uint32_t transaction_id = 0x4F534448;
static uint8_t response_received;
static uint8_t response_type;
static uint32_t server_identifier;
static uint8_t request_packet[548];

static void write_be32(uint8_t* p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static uint32_t read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8) | p[3];
}

static void add_option(uint16_t* offset, uint8_t code, const void* data,
                       uint8_t length) {
    request_packet[(*offset)++] = code;
    request_packet[(*offset)++] = length;
    for (uint8_t i = 0; i < length; i++)
        request_packet[(*offset)++] = ((const uint8_t*)data)[i];
}

static void dhcp_receive(udp_socket_t* socket, uint32_t source_ip,
                         uint16_t source_port, const uint8_t* data,
                         uint16_t length) {
    (void)socket;
    (void)source_ip;
    (void)source_port;
    if (length < 240 || read_be32(data + 4) != transaction_id) return;
    if (data[0] != DHCP_BOOTREPLY || data[1] != 1) return;
    for (uint16_t i = 240; i + 1 < length;) {
        uint8_t code = data[i++];
        uint8_t option_length;
        if (code == DHCP_OPTION_END) break;
        if (code == 0 || i >= length) continue;
        option_length = data[i++];
        if ((uint32_t)i + option_length > length) return;
        if (code == DHCP_OPTION_MESSAGE_TYPE && option_length == 1)
            response_type = data[i];
        else if (code == DHCP_OPTION_SUBNET_MASK && option_length == 4)
            current_lease.subnet_mask = read_be32(data + i);
        else if (code == DHCP_OPTION_ROUTER && option_length >= 4)
            current_lease.gateway = read_be32(data + i);
        else if (code == DHCP_OPTION_SERVER_ID && option_length == 4)
            server_identifier = read_be32(data + i);
        else if (code == DHCP_OPTION_DNS && option_length >= 4)
            current_lease.dns = read_be32(data + i);
        else if (code == DHCP_OPTION_LEASE_TIME && option_length == 4)
            current_lease.lease_seconds = read_be32(data + i);
        i += option_length;
    }
    current_lease.address = read_be32(data + 16);
    response_received = 1;
}

static uint8_t send_message(uint8_t message_type, uint32_t requested_ip) {
    uint16_t offset = 240;
    uint8_t type = message_type;
    uint8_t server[4];
    uint8_t requested[4];

    for (uint16_t i = 0; i < sizeof(request_packet); i++) request_packet[i] = 0;
    request_packet[0] = DHCP_BOOTREQUEST;
    request_packet[1] = 1;
    request_packet[2] = 6;
    write_be32(request_packet + 4, transaction_id);
    request_packet[10] = 0x80;
    {
        uint64_t mac = net_get_mac_address();
        for (uint8_t i = 0; i < 6; i++)
            request_packet[28 + i] = (uint8_t)(mac >> ((5 - i) * 8));
    }
    write_be32(request_packet + 236, DHCP_MAGIC);
    add_option(&offset, DHCP_OPTION_MESSAGE_TYPE, &type, 1);
    if (message_type == DHCP_REQUEST) {
        write_be32(server, server_identifier);
        write_be32(requested, requested_ip);
        add_option(&offset, DHCP_OPTION_SERVER_ID, server, 4);
        add_option(&offset, 50, requested, 4);
    }
    request_packet[offset++] = DHCP_OPTION_END;
    return udp_send(dhcp_socket, request_packet, offset);
}

uint8_t dhcp_request(dhcp_lease_t* lease, uint32_t wait_iterations) {
    dhcp_socket = udp_connect(0xFFFFFFFF, DHCP_SERVER_PORT);
    if (!dhcp_socket) return 0;
    udp_set_local_port(dhcp_socket, DHCP_CLIENT_PORT);
    udp_bind(dhcp_socket, dhcp_receive);
    current_lease.leased = 0;
    current_lease.subnet_mask = 0xFFFFFF00;
    current_lease.gateway = 0;
    current_lease.dns = 0;
    response_received = 0;
    response_type = 0;
    server_identifier = 0;
    transaction_id++;
    if (!send_message(DHCP_DISCOVER, 0)) return 0;
    for (uint32_t i = 0; i < wait_iterations && !response_received; i++) {
        net_poll();
        __asm__ volatile ("pause");
    }
    if (!response_received || response_type != DHCP_OFFER) return 0;
    response_received = 0;
    if (!send_message(DHCP_REQUEST, current_lease.address)) return 0;
    for (uint32_t i = 0; i < wait_iterations && !response_received; i++) {
        net_poll();
        __asm__ volatile ("pause");
    }
    if (!response_received || response_type != DHCP_ACK) return 0;
    current_lease.server = server_identifier;
    current_lease.leased = 1;
    ipv4_set_configuration(current_lease.address, current_lease.gateway,
                           current_lease.subnet_mask);
    if (lease) *lease = current_lease;
    return 1;
}

const dhcp_lease_t* dhcp_get_lease(void) {
    return &current_lease;
}

#include "../mutex.h"
#include "udp.h"
#include "ipv4.h"

struct udp_socket {
    uint8_t in_use;
    uint8_t listening;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t local_ip;
    uint32_t remote_ip;
    udp_data_handler_t handler;
};

static udp_socket_t sockets[UDP_MAX_SOCKETS];
static uint16_t next_ephemeral_port = 1024;
static uint8_t transmit_buffer[IPV4_MAX_PAYLOAD];
static uint8_t checksum_buffer[12 + IPV4_MAX_PAYLOAD];

static uint16_t read_be16(const uint8_t* data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static void write_be16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void write_be32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static udp_socket_t* allocate_socket(void) {
    for (uint32_t i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (!sockets[i].in_use) {
            sockets[i].in_use = 1;
            sockets[i].listening = 0;
            sockets[i].handler = 0;
            return &sockets[i];
        }
    }
    return 0;
}

static udp_socket_t* find_socket(uint32_t source_ip, uint16_t source_port,
                                 uint32_t destination_ip,
                                 uint16_t destination_port) {
    udp_socket_t* listener = 0;

    for (uint32_t i = 0; i < UDP_MAX_SOCKETS; i++) {
        udp_socket_t* socket = &sockets[i];
        if (!socket->in_use || socket->local_port != destination_port
            || (socket->local_ip != 0 && socket->local_ip != destination_ip)) {
            continue;
        }

        if (!socket->listening
            && (socket->remote_ip == source_ip
                || socket->remote_ip == 0xFFFFFFFF)
            && socket->remote_port == source_port) {
            return socket;
        }
        if (socket->listening && !listener) listener = socket;
    }
    return listener;
}

static uint16_t udp_checksum(uint32_t source_ip, uint32_t destination_ip,
                             const uint8_t* packet, uint16_t length) {
    write_be32(checksum_buffer, source_ip);
    write_be32(checksum_buffer + 4, destination_ip);
    checksum_buffer[8] = 0;
    checksum_buffer[9] = UDP_PROTOCOL;
    write_be16(checksum_buffer + 10, length);

    for (uint16_t i = 0; i < length; i++) {
        checksum_buffer[12 + i] = packet[i];
    }

    return ipv4_checksum(checksum_buffer, 12 + length);
}

static uint8_t udp_receive(uint32_t source_ip, uint32_t destination_ip,
                           const uint8_t* packet, uint32_t length) {
    udp_socket_t* socket;
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t packet_length;
    uint16_t checksum;
    uint16_t payload_length;

    if (!packet || length < UDP_HEADER_SIZE || length > IPV4_MAX_PAYLOAD) {
        return 0;
    }

    source_port = read_be16(packet);
    destination_port = read_be16(packet + 2);
    packet_length = read_be16(packet + 4);
    checksum = read_be16(packet + 6);

    if (packet_length < UDP_HEADER_SIZE || packet_length > length) return 0;

    /*
     * IPv4 permits a zero UDP checksum. A non-zero checksum must validate.
     * A calculated zero is transmitted as 0xFFFF by UDP convention.
     */
    if (checksum != 0 && udp_checksum(source_ip, destination_ip,
                                      packet, packet_length) != 0) {
        return 0;
    }

    socket = find_socket(source_ip, source_port, destination_ip, destination_port);
    if (!socket || !socket->handler) return 0;

    payload_length = packet_length - UDP_HEADER_SIZE;
    socket->handler(socket, source_ip, source_port,
                    packet + UDP_HEADER_SIZE, payload_length);
    return 0;
}

void udp_init(void) {
    MUTEX_GUARD(kernel_mutex);
    for (uint32_t i = 0; i < UDP_MAX_SOCKETS; i++) sockets[i].in_use = 0;
    next_ephemeral_port = 1024;
    ipv4_set_protocol_handler(UDP_PROTOCOL, udp_receive);
}

udp_socket_t* udp_connect(uint32_t remote_ip, uint16_t remote_port) {
    MUTEX_GUARD(kernel_mutex);
    udp_socket_t* socket = allocate_socket();
    if (!socket) return 0;

    socket->local_ip = ipv4_get_local_ip();
    socket->local_port = next_ephemeral_port++;
    if (next_ephemeral_port < 1024) next_ephemeral_port = 1024;
    socket->remote_ip = remote_ip;
    socket->remote_port = remote_port;
    return socket;
}

udp_socket_t* udp_listen(uint16_t local_port) {
    MUTEX_GUARD(kernel_mutex);
    udp_socket_t* socket = allocate_socket();
    if (!socket) return 0;

    socket->local_ip = ipv4_get_local_ip();
    socket->local_port = local_port;
    socket->listening = 1;
    return socket;
}

void udp_bind(udp_socket_t* socket, udp_data_handler_t handler) {
    MUTEX_GUARD(kernel_mutex);
    if (socket && socket->in_use) socket->handler = handler;
}

void udp_set_local_port(udp_socket_t* socket, uint16_t port) {
    MUTEX_GUARD(kernel_mutex);
    if (socket && socket->in_use && port != 0) socket->local_port = port;
}

uint8_t udp_send(udp_socket_t* socket, const uint8_t* data, uint16_t length) {
    MUTEX_GUARD(kernel_mutex);
    uint16_t packet_length;
    uint16_t checksum;

    if (!socket || !socket->in_use || socket->listening
        || !socket->remote_ip || length > UDP_MAX_PAYLOAD
        || (!data && length != 0)) {
        return 0;
    }

    packet_length = UDP_HEADER_SIZE + length;
    write_be16(transmit_buffer, socket->local_port);
    write_be16(transmit_buffer + 2, socket->remote_port);
    write_be16(transmit_buffer + 4, packet_length);
    write_be16(transmit_buffer + 6, 0);

    for (uint16_t i = 0; i < length; i++) {
        transmit_buffer[UDP_HEADER_SIZE + i] = data[i];
    }

    checksum = udp_checksum(socket->local_ip, socket->remote_ip,
                            transmit_buffer, packet_length);
    if (checksum == 0) checksum = 0xFFFF;
    write_be16(transmit_buffer + 6, checksum);

    return ipv4_send(socket->remote_ip, UDP_PROTOCOL,
                     transmit_buffer, packet_length);
}

void udp_disconnect(udp_socket_t* socket) {
    MUTEX_GUARD(kernel_mutex);
    if (socket && socket->in_use) socket->in_use = 0;
}

uint16_t udp_get_local_port(const udp_socket_t* socket) {
    MUTEX_GUARD(kernel_mutex);
    return socket && socket->in_use ? socket->local_port : 0;
}

uint16_t udp_get_remote_port(const udp_socket_t* socket) {
    MUTEX_GUARD(kernel_mutex);
    return socket && socket->in_use ? socket->remote_port : 0;
}

uint32_t udp_get_remote_ip(const udp_socket_t* socket) {
    MUTEX_GUARD(kernel_mutex);
    return socket && socket->in_use ? socket->remote_ip : 0;
}

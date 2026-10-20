#include "tcp.h"
#include "ipv4.h"

#define TCP_HEADER_SIZE 20
#define TCP_PSEUDO_HEADER_SIZE 12
#define TCP_WINDOW_SIZE 4096

struct tcp_socket {
    uint8_t in_use;
    tcp_socket_state_t state;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t local_ip;
    uint32_t remote_ip;
    uint32_t send_next;
    uint32_t send_unacknowledged;
    uint32_t receive_next;
    tcp_data_handler_t handler;
};

static tcp_socket_t sockets[TCP_MAX_SOCKETS];
static uint16_t next_ephemeral_port = 1024;
static uint32_t initial_sequence = 0xC001CAFE;
static uint8_t segment_buffer[IPV4_MAX_PAYLOAD];
static uint8_t checksum_buffer[TCP_PSEUDO_HEADER_SIZE + IPV4_MAX_PAYLOAD];

static uint16_t read_be16(const uint8_t* data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t read_be32(const uint8_t* data) {
    return ((uint32_t)data[0] << 24)
         | ((uint32_t)data[1] << 16)
         | ((uint32_t)data[2] << 8)
         | data[3];
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

static tcp_socket_t* allocate_socket(void) {
    for (uint32_t i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (!sockets[i].in_use) {
            sockets[i].in_use = 1;
            sockets[i].state = TCP_CLOSED;
            sockets[i].handler = 0;
            return &sockets[i];
        }
    }
    return 0;
}

static void release_socket(tcp_socket_t* socket) {
    if (socket) socket->in_use = 0;
}

static tcp_socket_t* find_socket(uint32_t source_ip, uint16_t source_port,
                                 uint32_t destination_ip, uint16_t destination_port) {
    tcp_socket_t* listener = 0;

    for (uint32_t i = 0; i < TCP_MAX_SOCKETS; i++) {
        tcp_socket_t* socket = &sockets[i];
        if (!socket->in_use || socket->local_port != destination_port) continue;

        if (socket->state != TCP_LISTEN
            && socket->remote_ip == source_ip
            && socket->remote_port == source_port
            && socket->local_ip == destination_ip) {
            return socket;
        }
        if (socket->state == TCP_LISTEN) listener = socket;
    }
    return listener;
}

static uint16_t tcp_checksum(uint32_t source_ip, uint32_t destination_ip,
                             const uint8_t* segment, uint16_t length) {
    write_be32(checksum_buffer, source_ip);
    write_be32(checksum_buffer + 4, destination_ip);
    checksum_buffer[8] = 0;
    checksum_buffer[9] = TCP_PROTOCOL;
    write_be16(checksum_buffer + 10, length);
    for (uint16_t i = 0; i < length; i++) {
        checksum_buffer[TCP_PSEUDO_HEADER_SIZE + i] = segment[i];
    }
    return ipv4_checksum(checksum_buffer, TCP_PSEUDO_HEADER_SIZE + length);
}

static uint8_t send_segment(tcp_socket_t* socket, const uint8_t* data,
                            uint16_t length, uint8_t flags) {
    uint16_t segment_length = TCP_HEADER_SIZE + length;
    uint32_t sequence = socket->send_next;
    uint8_t consumes_sequence = (flags & TCP_FLAG_SYN) != 0
                              || (flags & TCP_FLAG_FIN) != 0;

    if (segment_length > IPV4_MAX_PAYLOAD || length > TCP_MAX_PAYLOAD) return 0;

    write_be16(segment_buffer, socket->local_port);
    write_be16(segment_buffer + 2, socket->remote_port);
    write_be32(segment_buffer + 4, sequence);
    write_be32(segment_buffer + 8, socket->receive_next);
    segment_buffer[12] = (uint8_t)(5 << 4);
    segment_buffer[13] = flags;
    write_be16(segment_buffer + 14, TCP_WINDOW_SIZE);
    write_be16(segment_buffer + 16, 0);
    write_be16(segment_buffer + 18, 0);
    for (uint16_t i = 0; i < length; i++) {
        segment_buffer[TCP_HEADER_SIZE + i] = data[i];
    }
    write_be16(segment_buffer + 16,
               tcp_checksum(socket->local_ip, socket->remote_ip,
                            segment_buffer, segment_length));

    if (!ipv4_send(socket->remote_ip, TCP_PROTOCOL, segment_buffer,
                   segment_length)) {
        return 0;
    }
    if (consumes_sequence) socket->send_next++;
    socket->send_next += length;
    return 1;
}

static uint8_t tcp_receive(uint32_t source_ip, uint32_t destination_ip,
                            const uint8_t* segment, uint32_t length) {
    tcp_socket_t* socket;
    uint16_t source_port;
    uint16_t destination_port;
    uint32_t sequence;
    uint32_t acknowledgement;
    uint16_t header_length;
    uint16_t payload_length;
    uint8_t flags;

    if (!segment || length < TCP_HEADER_SIZE || length > IPV4_MAX_PAYLOAD) return 0;
    if (tcp_checksum(source_ip, destination_ip, segment, (uint16_t)length) != 0) {
        return 0;
    }

    source_port = read_be16(segment);
    destination_port = read_be16(segment + 2);
    sequence = read_be32(segment + 4);
    acknowledgement = read_be32(segment + 8);
    header_length = (uint16_t)((segment[12] >> 4) * 4);
    flags = segment[13];
    if (header_length < TCP_HEADER_SIZE || header_length > length) return 0;

    socket = find_socket(source_ip, source_port, destination_ip, destination_port);
    if (flags & TCP_FLAG_RST) {
        if (socket) release_socket(socket);
        return 0;
    }

    if (!socket) {
        tcp_socket_t temporary;
        temporary.in_use = 1;
        temporary.state = TCP_CLOSED;
        temporary.local_ip = destination_ip;
        temporary.remote_ip = source_ip;
        temporary.local_port = destination_port;
        temporary.remote_port = source_port;
        temporary.send_next = 0;
        temporary.receive_next = sequence + ((flags & TCP_FLAG_SYN) ? 1 : 0);
        temporary.handler = 0;
        if (flags & TCP_FLAG_SYN) {
            send_segment(&temporary, 0, 0, TCP_FLAG_RST | TCP_FLAG_ACK);
        }
        return 0;
    }

    if (socket->state == TCP_LISTEN) {
        tcp_socket_t* child;
        if (!(flags & TCP_FLAG_SYN)) return 0;
        child = allocate_socket();
        if (!child) return 0;
        child->local_ip = destination_ip;
        child->local_port = destination_port;
        child->remote_ip = source_ip;
        child->remote_port = source_port;
        child->receive_next = sequence + 1;
        child->send_unacknowledged = initial_sequence++;
        child->send_next = child->send_unacknowledged;
        child->handler = socket->handler;
        child->state = TCP_SYN_RECEIVED;
        if (!send_segment(child, 0, 0, TCP_FLAG_SYN | TCP_FLAG_ACK)) {
            release_socket(child);
        }
        return 0;
    }

    if ((flags & TCP_FLAG_ACK) && acknowledgement > socket->send_next) {
        release_socket(socket);
        return 0;
    }

    if (socket->state == TCP_SYN_SENT) {
        if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK))
            != (TCP_FLAG_SYN | TCP_FLAG_ACK)
            || acknowledgement != socket->send_next) {
            return 0;
        }
        socket->receive_next = sequence + 1;
        socket->send_unacknowledged = acknowledgement;
        socket->state = TCP_ESTABLISHED;
        send_segment(socket, 0, 0, TCP_FLAG_ACK);
        return 0;
    }

    if (socket->state == TCP_SYN_RECEIVED) {
        if (!(flags & TCP_FLAG_ACK) || acknowledgement != socket->send_next) {
            return 0;
        }
        socket->send_unacknowledged = acknowledgement;
        socket->state = TCP_ESTABLISHED;
    }

    if ((flags & TCP_FLAG_FIN) != 0) {
        if (sequence != socket->receive_next) return 0;
        socket->receive_next++;
        send_segment(socket, 0, 0, TCP_FLAG_ACK);
        if (socket->state == TCP_ESTABLISHED) {
            socket->state = TCP_CLOSE_WAIT;
            send_segment(socket, 0, 0, TCP_FLAG_FIN | TCP_FLAG_ACK);
        } else {
            release_socket(socket);
        }
        return 0;
    }

    payload_length = (uint16_t)(length - header_length);
    if (payload_length == 0) return 0;
    if (sequence != socket->receive_next || !socket->handler) return 0;
    if (socket->handler(socket, segment + header_length, payload_length)) {
        socket->receive_next += payload_length;
        send_segment(socket, 0, 0, TCP_FLAG_ACK);
    }
    return 0;
}

void tcp_init(void) {
    for (uint32_t i = 0; i < TCP_MAX_SOCKETS; i++) sockets[i].in_use = 0;
    next_ephemeral_port = 1024;
    initial_sequence = 0xC001CAFE;
    ipv4_set_protocol_handler(TCP_PROTOCOL, tcp_receive);
}

tcp_socket_t* tcp_connect(uint32_t remote_ip, uint16_t remote_port) {
    tcp_socket_t* socket = allocate_socket();
    if (!socket) return 0;

    socket->local_ip = ipv4_get_local_ip();
    socket->remote_ip = remote_ip;
    socket->local_port = next_ephemeral_port++;
    if (next_ephemeral_port < 1024) next_ephemeral_port = 1024;
    socket->remote_port = remote_port;
    socket->send_unacknowledged = initial_sequence++;
    socket->send_next = socket->send_unacknowledged;
    socket->receive_next = 0;
    socket->state = TCP_SYN_SENT;

    if (!send_segment(socket, 0, 0, TCP_FLAG_SYN)) {
        release_socket(socket);
        return 0;
    }
    return socket;
}

tcp_socket_t* tcp_listen(uint16_t local_port) {
    tcp_socket_t* socket = allocate_socket();
    if (!socket) return 0;

    socket->local_ip = ipv4_get_local_ip();
    socket->local_port = local_port;
    socket->state = TCP_LISTEN;
    return socket;
}

void tcp_bind(tcp_socket_t* socket, tcp_data_handler_t handler) {
    if (socket && socket->in_use) socket->handler = handler;
}

uint8_t tcp_send(tcp_socket_t* socket, const uint8_t* data, uint16_t length) {
    if (!socket || !socket->in_use || socket->state != TCP_ESTABLISHED
        || (!data && length != 0)) {
        return 0;
    }
    return send_segment(socket, data, length, TCP_FLAG_PSH | TCP_FLAG_ACK);
}

void tcp_disconnect(tcp_socket_t* socket) {
    if (!socket || !socket->in_use) return;
    if (socket->state == TCP_ESTABLISHED || socket->state == TCP_CLOSE_WAIT) {
        if (send_segment(socket, 0, 0, TCP_FLAG_FIN | TCP_FLAG_ACK)) {
            socket->state = TCP_FIN_WAIT1;
        }
    }
}

void tcp_release(tcp_socket_t* socket) {
    if (!socket || !socket->in_use) return;
    tcp_disconnect(socket);
    release_socket(socket);
}

tcp_socket_state_t tcp_get_state(const tcp_socket_t* socket) {
    if (!socket || !socket->in_use) return TCP_CLOSED;
    return socket->state;
}

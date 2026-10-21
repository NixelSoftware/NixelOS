#include "../mutex.h"
#include "icmp.h"
#include "ipv4.h"

static uint8_t transmit_packet[ICMP_MAX_PACKET_SIZE];
static icmp_echo_reply_handler_t echo_reply_handler;

static uint16_t read_be16(const uint8_t* data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static void write_be16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static uint8_t icmp_receive(uint32_t source_ip, uint32_t destination_ip,
                            const uint8_t* packet, uint32_t length) {
    uint8_t type;
    uint8_t code;
    uint16_t identifier;
    uint16_t sequence;
    uint16_t payload_length;

    (void)destination_ip;
    if (!packet || length < ICMP_ECHO_HEADER_SIZE
        || length > ICMP_MAX_PACKET_SIZE) {
        return 0;
    }
    if (icmp_checksum(packet, (uint16_t)length) != 0) return 0;

    type = packet[0];
    code = packet[1];
    if (code != 0) return 0;

    identifier = read_be16(packet + 4);
    sequence = read_be16(packet + 6);
    payload_length = (uint16_t)(length - ICMP_ECHO_HEADER_SIZE);

    if (type == ICMP_ECHO_REQUEST) {
        for (uint16_t i = 0; i < length; i++) transmit_packet[i] = packet[i];
        transmit_packet[0] = ICMP_ECHO_REPLY;
        write_be16(transmit_packet + 2, 0);
        write_be16(transmit_packet + 2,
                   icmp_checksum(transmit_packet, (uint16_t)length));
        return ipv4_send(source_ip, ICMP_PROTOCOL, transmit_packet, length);
    }

    if (type == ICMP_ECHO_REPLY && echo_reply_handler) {
        echo_reply_handler(source_ip, identifier, sequence,
                           packet + ICMP_ECHO_HEADER_SIZE, payload_length);
    }
    return 0;
}

uint16_t icmp_checksum(const void* data, uint16_t length) {
    MUTEX_GUARD(kernel_mutex);
    return ipv4_checksum(data, length);
}

void icmp_init(void) {
    MUTEX_GUARD(kernel_mutex);
    echo_reply_handler = 0;
    ipv4_set_protocol_handler(ICMP_PROTOCOL, icmp_receive);
}

void icmp_set_echo_reply_handler(icmp_echo_reply_handler_t handler) {
    MUTEX_GUARD(kernel_mutex);
    echo_reply_handler = handler;
}

uint8_t icmp_request_echo(uint32_t destination_ip, uint16_t identifier,
                          uint16_t sequence, const uint8_t* payload,
                          uint16_t length) {
    MUTEX_GUARD(kernel_mutex);
    uint16_t packet_length;

    if (length > ICMP_MAX_PACKET_SIZE - ICMP_ECHO_HEADER_SIZE
        || (!payload && length != 0)) {
        return 0;
    }

    packet_length = ICMP_ECHO_HEADER_SIZE + length;
    transmit_packet[0] = ICMP_ECHO_REQUEST;
    transmit_packet[1] = 0;
    write_be16(transmit_packet + 2, 0);
    write_be16(transmit_packet + 4, identifier);
    write_be16(transmit_packet + 6, sequence);

    for (uint16_t i = 0; i < length; i++) {
        transmit_packet[ICMP_ECHO_HEADER_SIZE + i] = payload[i];
    }

    write_be16(transmit_packet + 2,
               icmp_checksum(transmit_packet, packet_length));
    return ipv4_send(destination_ip, ICMP_PROTOCOL,
                     transmit_packet, packet_length);
}

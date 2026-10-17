#ifndef ICMP_H
#define ICMP_H

#include "../types.h"
#include "ipv4.h"

#define ICMP_PROTOCOL 1
#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_HEADER_SIZE 8
#define ICMP_MAX_PACKET_SIZE IPV4_MAX_PAYLOAD

typedef void (*icmp_echo_reply_handler_t)(uint32_t source_ip,
                                           uint16_t identifier,
                                           uint16_t sequence,
                                           const uint8_t* payload,
                                           uint16_t length);

void icmp_init(void);
void icmp_set_echo_reply_handler(icmp_echo_reply_handler_t handler);
uint8_t icmp_request_echo(uint32_t destination_ip, uint16_t identifier,
                          uint16_t sequence, const uint8_t* payload,
                          uint16_t length);
uint16_t icmp_checksum(const void* data, uint16_t length);

#endif

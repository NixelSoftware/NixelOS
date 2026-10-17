#ifndef IPV4_H
#define IPV4_H

#include "../types.h"

#define IPV4_ETHERTYPE 0x0800
#define IPV4_HEADER_MIN_SIZE 20
#define IPV4_MAX_PAYLOAD 1468

typedef uint8_t (*ipv4_protocol_handler_t)(uint32_t source_ip,
                                            uint32_t destination_ip,
                                            const uint8_t* payload,
                                            uint32_t length);
typedef uint8_t (*ipv4_mac_resolver_t)(uint32_t ip_address,
                                        uint8_t mac[6]);

typedef struct {
    uint8_t destination[6];
    uint8_t source[6];
    uint16_t ether_type;
} __attribute__((packed)) ethernet_frame_header_t;

typedef struct {
    uint8_t version_and_header_length;
    uint8_t type_of_service;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_and_fragment_offset;
    uint8_t time_to_live;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t source_ip;
    uint32_t destination_ip;
} __attribute__((packed)) ipv4_header_t;

void ipv4_init(uint32_t local_ip, const uint8_t local_mac[6],
               uint32_t gateway_ip, uint32_t subnet_mask);
void ipv4_set_mac_resolver(ipv4_mac_resolver_t resolver);
void ipv4_set_protocol_handler(uint8_t protocol,
                               ipv4_protocol_handler_t handler);
void ipv4_set_configuration(uint32_t local_ip, uint32_t gateway_ip,
                            uint32_t subnet_mask);

uint8_t ipv4_receive_frame(const uint8_t* frame, uint32_t length);
uint8_t ipv4_send(uint32_t destination_ip, uint8_t protocol,
                  const uint8_t* payload, uint32_t length);

uint16_t ipv4_checksum(const void* data, uint32_t length);
uint32_t ipv4_get_local_ip(void);

#endif

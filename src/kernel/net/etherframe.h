#ifndef ETHERFRAME_H
#define ETHERFRAME_H

#include "../types.h"
#include "../hal/net_types.h"

#define ETHERFRAME_HEADER_SIZE 14
#define ETHERFRAME_MAX_PAYLOAD (NET_MAX_FRAME_SIZE - ETHERFRAME_HEADER_SIZE)
#define ETHERFRAME_BROADCAST_MAC 0xFFFFFFFFFFFFULL

typedef uint8_t (*etherframe_handler_t)(const uint8_t source_mac[6],
                                         const uint8_t destination_mac[6],
                                         const uint8_t* payload,
                                         uint32_t length);

void etherframe_init(const uint8_t local_mac[6]);
void etherframe_set_handler(uint16_t ether_type,
                            etherframe_handler_t handler);
uint8_t etherframe_receive(const uint8_t* frame, uint32_t length);
uint8_t etherframe_send(uint64_t destination_mac, uint16_t ether_type,
                        const uint8_t* payload, uint32_t length);
uint64_t etherframe_get_mac_address(void);

uint64_t etherframe_mac_from_bytes(const uint8_t mac[6]);
void etherframe_mac_to_bytes(uint64_t mac, uint8_t output[6]);

#endif

#ifndef ARP_H
#define ARP_H

#include "../types.h"

#define ARP_ETHERNET 1
#define ARP_IPV4_ETHERTYPE 0x0800
#define ARP_REQUEST 1
#define ARP_REPLY 2
#define ARP_CACHE_SIZE 128

void arp_init(uint32_t local_ip, const uint8_t local_mac[6]);
uint8_t arp_receive(const uint8_t source_mac[6],
                    const uint8_t destination_mac[6],
                    const uint8_t* payload, uint32_t length);
uint8_t arp_resolve(uint32_t ip_address, uint8_t mac[6]);
void arp_request(uint32_t ip_address);
void arp_clear_cache(void);

#endif

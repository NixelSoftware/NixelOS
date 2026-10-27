#include "../mutex.h"
#include "arp.h"
#include "etherframe.h"
#include "ipv4.h"
#include "../hal/net.h"

#define ARP_PACKET_SIZE 28
extern volatile uint32_t frame_timer;
static uint32_t request_ip[ARP_CACHE_SIZE], request_tick[ARP_CACHE_SIZE];
static uint8_t request_pending[ARP_CACHE_SIZE];

typedef struct {
    uint32_t ip;
    uint8_t mac[6];
    uint8_t valid;
} arp_cache_entry_t;

static uint32_t local_ip;
static uint8_t local_mac[6];
static arp_cache_entry_t cache[ARP_CACHE_SIZE];

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

static void cache_insert(uint32_t ip, const uint8_t mac[6]) {
    uint32_t slot = ARP_CACHE_SIZE;

    for (uint32_t i = 0; i < ARP_CACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].ip == ip) {
            slot = i;
            break;
        }
        if (!cache[i].valid && slot == ARP_CACHE_SIZE) slot = i;
    }
    if (slot == ARP_CACHE_SIZE) slot = ip % ARP_CACHE_SIZE;

    cache[slot].ip = ip;
    for (uint8_t i = 0; i < 6; i++) cache[slot].mac[i] = mac[i];
    cache[slot].valid = 1;
}

void arp_init(uint32_t address, const uint8_t address_mac[6]) {
    MUTEX_GUARD(kernel_mutex);
    local_ip = address;
    for (uint8_t i = 0; i < 6; i++) local_mac[i] = address_mac[i];
    arp_clear_cache();
    etherframe_set_handler(0x0806, arp_receive);
    ipv4_set_mac_resolver(arp_resolve);
}

void arp_clear_cache(void) {
    MUTEX_GUARD(kernel_mutex);
    for (uint32_t i = 0; i < ARP_CACHE_SIZE; i++) {
        cache[i].valid = 0; request_pending[i] = 0;
    }
}

void arp_set_local_ip(uint32_t address) {
    MUTEX_GUARD(kernel_mutex);
    local_ip = address;
}

uint8_t arp_receive(const uint8_t source_mac[6],
                    const uint8_t destination_mac[6],
                    const uint8_t* payload, uint32_t length) {
    MUTEX_GUARD(kernel_mutex);
    uint16_t hardware_type;
    uint16_t protocol_type;
    uint16_t operation;
    uint32_t sender_ip;
    uint32_t target_ip;
    uint8_t response[ARP_PACKET_SIZE];

    (void)source_mac;
    (void)destination_mac;
    if (!payload || length < ARP_PACKET_SIZE) return 0;

    hardware_type = read_be16(payload);
    protocol_type = read_be16(payload + 2);
    operation = read_be16(payload + 6);
    if (hardware_type != ARP_ETHERNET || protocol_type != ARP_IPV4_ETHERTYPE
        || payload[4] != 6 || payload[5] != 4
        || (operation != ARP_REQUEST && operation != ARP_REPLY)) {
        return 0;
    }

    sender_ip = read_be32(payload + 14);
    target_ip = read_be32(payload + 24);
    cache_insert(sender_ip, payload + 8);

    if (operation != ARP_REQUEST || target_ip != local_ip) return 0;

    write_be16(response, ARP_ETHERNET);
    write_be16(response + 2, ARP_IPV4_ETHERTYPE);
    response[4] = 6;
    response[5] = 4;
    write_be16(response + 6, ARP_REPLY);
    for (uint8_t i = 0; i < 6; i++) {
        response[8 + i] = local_mac[i];
        response[18 + i] = payload[8 + i];
    }
    write_be32(response + 14, local_ip);
    write_be32(response + 24, sender_ip);

    return etherframe_send(etherframe_mac_from_bytes(payload + 8),
                           0x0806, response, ARP_PACKET_SIZE);
}

void arp_request(uint32_t ip_address) {
    MUTEX_GUARD(kernel_mutex);
    uint8_t request[ARP_PACKET_SIZE];
    for (uint8_t i = 0; i < 6; i++) {
        request[i] = local_mac[i];
        request[18 + i] = 0;
    }
    write_be16(request, ARP_ETHERNET);
    write_be16(request + 2, ARP_IPV4_ETHERTYPE);
    request[4] = 6;
    request[5] = 4;
    write_be16(request + 6, ARP_REQUEST);
    for (uint8_t i = 0; i < 6; i++) request[8 + i] = local_mac[i];
    write_be32(request + 14, local_ip);
    write_be32(request + 24, ip_address);
    etherframe_send(ETHERFRAME_BROADCAST_MAC, 0x0806,
                    request, ARP_PACKET_SIZE);
}

uint8_t arp_resolve(uint32_t ip_address, uint8_t mac[6]) {
    MUTEX_GUARD(kernel_mutex);
    if (ip_address == 0xFFFFFFFFU) {
        for (uint8_t i = 0; i < 6; i++) mac[i] = 0xFF;
        return 1;
    }
    for (uint32_t i = 0; i < ARP_CACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].ip == ip_address) {
            for (uint8_t j = 0; j < 6; j++) mac[j] = cache[i].mac[j];
            return 1;
        }
    }
    /* Cache miss is asynchronous. IPv4 owns a bounded copy of the outgoing
     * packet and retries after the desktop network pump receives the reply. */
    uint32_t slot = ip_address % ARP_CACHE_SIZE;
    if (!request_pending[slot] || request_ip[slot] != ip_address ||
        (uint32_t)(frame_timer - request_tick[slot]) >= 100u) {
        request_pending[slot] = 1;
        request_ip[slot] = ip_address;
        request_tick[slot] = frame_timer;
        arp_request(ip_address);
    }
    return 0;
}

#include "ipv4.h"
#include "etherframe.h"
#include "arp.h"
#include "../kernel_net.h"

#define IPV4_ETHERNET_HEADER_SIZE 14
#define IPV4_MAX_FRAME_SIZE (IPV4_ETHERNET_HEADER_SIZE + IPV4_HEADER_MIN_SIZE + IPV4_MAX_PAYLOAD)
#define IPV4_DEFAULT_TTL 64

static uint32_t local_ip;
static uint32_t gateway_ip;
static uint32_t subnet_mask;
static uint8_t local_mac[6];
static ipv4_protocol_handler_t protocol_handlers[256];
static ipv4_mac_resolver_t mac_resolver;
static uint8_t transmit_frame[IPV4_MAX_FRAME_SIZE];

static uint16_t swap16(uint16_t value) {
    return (uint16_t)((value << 8) | (value >> 8));
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

static uint8_t ipv4_receive_etherframe(const uint8_t source_mac[6],
                                       const uint8_t destination_mac[6],
                                       const uint8_t* payload,
                                       uint32_t length) {
    (void)source_mac;
    (void)destination_mac;
    if (!payload || length < IPV4_HEADER_MIN_SIZE) return 0;
    return ipv4_receive_frame(payload, length);
}

uint16_t ipv4_checksum(const void* data, uint32_t length) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t sum = 0;

    while (length >= 2) {
        sum += ((uint16_t)bytes[0] << 8) | bytes[1];
        bytes += 2;
        length -= 2;
    }
    if (length != 0) sum += (uint16_t)bytes[0] << 8;

    while (sum >> 16) sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint16_t)~sum;
}

void ipv4_init(uint32_t address, const uint8_t address_mac[6],
               uint32_t gateway, uint32_t mask) {
    local_ip = address;
    gateway_ip = gateway;
    subnet_mask = mask;
    mac_resolver = 0;

    for (uint16_t i = 0; i < 256; i++) protocol_handlers[i] = 0;
    for (uint8_t i = 0; i < 6; i++) local_mac[i] = address_mac[i];

    etherframe_init(address_mac);
    etherframe_set_handler(IPV4_ETHERTYPE, ipv4_receive_etherframe);
}

void ipv4_set_mac_resolver(ipv4_mac_resolver_t resolver) {
    mac_resolver = resolver;
}

void ipv4_set_configuration(uint32_t address, uint32_t gateway,
                            uint32_t mask) {
    local_ip = address;
    gateway_ip = gateway;
    subnet_mask = mask;
    net_set_ip_address(address);
    arp_set_local_ip(address);
}

void ipv4_set_protocol_handler(uint8_t protocol,
                               ipv4_protocol_handler_t handler) {
    protocol_handlers[protocol] = handler;
}

uint8_t ipv4_receive_frame(const uint8_t* frame, uint32_t length) {
    const ethernet_frame_header_t* ethernet;
    const ipv4_header_t* header;
    uint32_t header_length;
    uint32_t total_length;
    uint32_t payload_length;

    if (!frame || length < IPV4_HEADER_MIN_SIZE) {
        return 0;
    }

    ethernet = (const ethernet_frame_header_t*)frame;
    if (length >= IPV4_ETHERNET_HEADER_SIZE + IPV4_HEADER_MIN_SIZE
        && swap16(ethernet->ether_type) == IPV4_ETHERTYPE) {
        header = (const ipv4_header_t*)(frame + IPV4_ETHERNET_HEADER_SIZE);
        length -= IPV4_ETHERNET_HEADER_SIZE;
        frame += IPV4_ETHERNET_HEADER_SIZE;
    } else {
        header = (const ipv4_header_t*)frame;
    }
    if ((header->version_and_header_length >> 4) != 4) return 0;

    header_length = (uint32_t)(header->version_and_header_length & 0x0F) * 4;
    if (header_length < IPV4_HEADER_MIN_SIZE
        || length < header_length) {
        return 0;
    }

    total_length = ((uint32_t)((const uint8_t*)&header->total_length)[0] << 8)
                 | ((const uint8_t*)&header->total_length)[1];
    if (total_length < header_length
        || total_length > length) {
        return 0;
    }

    if (ipv4_checksum(header, header_length) != 0) return 0;
    {
        uint32_t destination_ip =
            read_be32((const uint8_t*)&header->destination_ip);
        if (destination_ip != local_ip && destination_ip != 0xFFFFFFFF) return 0;
    }

    if (swap16(header->flags_and_fragment_offset) & 0x1FFF) {
        return 0;
    }

    payload_length = total_length - header_length;
    if (protocol_handlers[header->protocol]) {
        protocol_handlers[header->protocol](
            read_be32((const uint8_t*)&header->source_ip),
            read_be32((const uint8_t*)&header->destination_ip),
            frame + header_length,
            payload_length);
    }
    return 0;
}

uint8_t ipv4_send(uint32_t destination_ip, uint8_t protocol,
                  const uint8_t* payload, uint32_t length) {
    ethernet_frame_header_t* ethernet;
    ipv4_header_t* header;
    uint8_t destination_mac[6];
    uint32_t route;
    uint32_t frame_length;

    if (!payload || length > IPV4_MAX_PAYLOAD) return 0;

    route = destination_ip == 0xFFFFFFFF
          ? destination_ip
          : (destination_ip & subnet_mask) == (local_ip & subnet_mask)
          ? destination_ip : gateway_ip;
    if (!mac_resolver || !mac_resolver(route, destination_mac)) return 0;

    ethernet = (ethernet_frame_header_t*)transmit_frame;
    for (uint8_t i = 0; i < 6; i++) {
        ethernet->destination[i] = destination_mac[i];
        ethernet->source[i] = local_mac[i];
    }
    ethernet->ether_type = swap16(IPV4_ETHERTYPE);

    header = (ipv4_header_t*)(transmit_frame + IPV4_ETHERNET_HEADER_SIZE);
    header->version_and_header_length = 0x45;
    header->type_of_service = 0;
    write_be16((uint8_t*)&header->total_length,
               (uint16_t)(IPV4_HEADER_MIN_SIZE + length));
    write_be16((uint8_t*)&header->identification, 0);
    write_be16((uint8_t*)&header->flags_and_fragment_offset, 0x4000);
    header->time_to_live = IPV4_DEFAULT_TTL;
    header->protocol = protocol;
    header->checksum = 0;
    write_be32((uint8_t*)&header->source_ip, local_ip);
    write_be32((uint8_t*)&header->destination_ip, destination_ip);
    write_be16((uint8_t*)&header->checksum,
               ipv4_checksum(header, IPV4_HEADER_MIN_SIZE));

    for (uint32_t i = 0; i < length; i++) {
        transmit_frame[IPV4_ETHERNET_HEADER_SIZE + IPV4_HEADER_MIN_SIZE + i]
            = payload[i];
    }

    frame_length = IPV4_ETHERNET_HEADER_SIZE + IPV4_HEADER_MIN_SIZE + length;
    return etherframe_send(etherframe_mac_from_bytes(destination_mac),
                           IPV4_ETHERTYPE,
                           transmit_frame + IPV4_ETHERNET_HEADER_SIZE,
                           frame_length - IPV4_ETHERNET_HEADER_SIZE);
}

uint32_t ipv4_get_local_ip(void) {
    return local_ip;
}

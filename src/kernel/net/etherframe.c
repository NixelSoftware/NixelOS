#include "../mutex.h"
#include "etherframe.h"
#include "../hal/net_types.h"
#include "../drivers/serial.h"
#include "../hal/net.h"

static uint8_t local_mac[6];
static etherframe_handler_t handlers[65536];
static uint8_t transmit_frame[NET_MAX_FRAME_SIZE];

static uint16_t read_be16(const uint8_t* data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static void write_be16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

uint64_t etherframe_mac_from_bytes(const uint8_t mac[6]) {
    MUTEX_GUARD(kernel_mutex);
    uint64_t result = 0;
    for (uint8_t i = 0; i < 6; i++) result = (result << 8) | mac[i];
    return result;
}

void etherframe_mac_to_bytes(uint64_t mac, uint8_t output[6]) {
    MUTEX_GUARD(kernel_mutex);
    for (int8_t i = 5; i >= 0; i--) {
        output[i] = (uint8_t)mac;
        mac >>= 8;
    }
}

void etherframe_init(const uint8_t address[6]) {
    MUTEX_GUARD(kernel_mutex);
    uint32_t return_address = (uint32_t)__builtin_return_address(0);
    debug_subsystem_print("[NET TRACE] etherframe_init return address: ");
    debug_subsystem_hex(return_address);
    debug_subsystem_print("\n");
    debug_subsystem_print("[NET TRACE] etherframe_init clearing handlers\n");
    for (uint32_t i = 0; i < 65536; i++) handlers[i] = 0;
    debug_subsystem_print("[NET TRACE] etherframe_init handlers cleared\n");
    for (uint8_t i = 0; i < 6; i++) local_mac[i] = address[i];
    debug_subsystem_print("[NET TRACE] etherframe_init registering receive handler\n");
    sys_net_set_receive_handler(etherframe_receive);
    debug_subsystem_print("[NET TRACE] etherframe_init receive handler registered\n");
    debug_subsystem_print("[NET TRACE] etherframe_init return address before return: ");
    debug_subsystem_hex((uint32_t)__builtin_return_address(0));
    debug_subsystem_print("\n");
}

void etherframe_set_handler(uint16_t ether_type,
                            etherframe_handler_t handler) {
    MUTEX_GUARD(kernel_mutex);
    handlers[ether_type] = handler;
}

uint8_t etherframe_receive(const uint8_t* frame, uint32_t length) {
    MUTEX_GUARD(kernel_mutex);
    uint16_t ether_type;
    uint64_t destination;
    etherframe_handler_t handler;

    if (!frame || length < ETHERFRAME_HEADER_SIZE) return 0;

    destination = etherframe_mac_from_bytes(frame);
    if (destination != ETHERFRAME_BROADCAST_MAC
        && destination != etherframe_mac_from_bytes(local_mac)) {
        return 0;
    }

    ether_type = read_be16(frame + 12);
    handler = handlers[ether_type];
    if (!handler) return 0;

    return handler(frame + 6, frame, frame + ETHERFRAME_HEADER_SIZE,
                   length - ETHERFRAME_HEADER_SIZE);
}

uint8_t etherframe_send(uint64_t destination_mac, uint16_t ether_type,
                        const uint8_t* payload, uint32_t length) {
    MUTEX_GUARD(kernel_mutex);
    if (!payload || length > ETHERFRAME_MAX_PAYLOAD) return 0;

    etherframe_mac_to_bytes(destination_mac, transmit_frame);
    for (uint8_t i = 0; i < 6; i++) transmit_frame[6 + i] = local_mac[i];
    write_be16(transmit_frame + 12, ether_type);
    for (uint32_t i = 0; i < length; i++) {
        transmit_frame[ETHERFRAME_HEADER_SIZE + i] = payload[i];
    }

    return sys_net_send(transmit_frame, ETHERFRAME_HEADER_SIZE + length);
}

uint64_t etherframe_get_mac_address(void) {
    MUTEX_GUARD(kernel_mutex);
    return etherframe_mac_from_bytes(local_mac);
}

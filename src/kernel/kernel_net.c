#include "kernel_net.h"
#include "drivers/net.h"
#include "drivers/net_e1000.h"
#include "drivers/serial.h"
#include "net/arp.h"
#include "net/dhcp.h"
#include "net/icmp.h"
#include "net/ipv4.h"
#include "net/udp.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    const char* name;
    uint32_t driver_id;
} pci_net_match_t;

// Supported network adapters
static const pci_net_match_t supported_cards[] = {
    // AMD PCNet-PCI II / FAST III
    { 0x1022, 0x2000, "AMD PCNet-PCI II (Am79C970A)", 0 },
    { 0x1022, 0x2001, "AMD PCNet-FAST III (Am79C973)", 0 },

    // Intel E1000 Family
    { 0x8086, 0x100E, "Intel 82540EM Gigabit Ethernet", 1 },
    { 0x8086, 0x100F, "Intel 82545EM Gigabit Ethernet", 1 },
    { 0x8086, 0x1004, "Intel 82543GC Gigabit Ethernet", 1 },
    { 0x8086, 0x1000, "Intel 82542 Gigabit Ethernet",    1 },
    { 0x8086, 0x10D3, "Intel 82574L Gigabit Ethernet",   1 },

    { 0, 0, 0, 0 } 
};

uint32_t net_driver_type = 0xFFFFFFFF;

static uint32_t pci_read_cfg(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

uint8_t sys_net_init(uint16_t io) {
    (void)io;
    net_driver_type = 0xFFFFFFFF;

    debug_print("[NET] Probing PCI bus for network adapters...\n");

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t id = pci_read_cfg((uint8_t)bus, slot, func, 0);
                uint16_t vendor = (uint16_t)id;
                uint16_t device = (uint16_t)(id >> 16);

                if (vendor == 0xFFFF || vendor == 0x0000) continue;

                for (int i = 0; supported_cards[i].vendor_id != 0; i++) {
                    if (supported_cards[i].vendor_id == vendor &&
                        supported_cards[i].device_id == device) {
                        debug_print("[NET] Detected: ");
                        debug_print(supported_cards[i].name);
                        debug_print("\n");

                        net_driver_type = supported_cards[i].driver_id;
                        goto adapter_matched;
                    }
                }
            }
        }
    }

adapter_matched:
    if (net_driver_type == 1) {
        return e1000_init();
    } else if (net_driver_type == 0) {
        return net_init(0);
    }

    debug_print("[NET] Warning: No supported network card found on PCI.\n");
    return 0;
}

uint8_t sys_net_is_ready(void) {
    if (net_driver_type == 1) return e1000_is_ready();
    if (net_driver_type == 0) return net_is_ready();
    return 0;
}

void sys_net_shutdown(void) {
    if (net_driver_type == 1) {
        e1000_shutdown();
    } else if (net_driver_type == 0) {
        net_shutdown();
    }
}

void sys_net_poll(void) {
    if (net_driver_type == 1) {
        e1000_poll();
    } else if (net_driver_type == 0) {
        net_poll();
    }
}

uint8_t sys_net_send(const uint8_t* data, uint32_t length) {
    if (net_driver_type == 1) return e1000_send(data, length);
    if (net_driver_type == 0) return net_send(data, length);
    return 0;
}

void sys_net_set_receive_handler(net_receive_handler_t handler) {
    if (net_driver_type == 1) {
        e1000_set_receive_handler(handler);
    } else if (net_driver_type == 0) {
        net_set_receive_handler(handler);
    }
}

uint64_t sys_net_get_mac_address(void) {
    if (net_driver_type == 1) return e1000_get_mac_address();
    if (net_driver_type == 0) return net_get_mac_address();
    return 0;
}

void sys_net_set_ip_address(uint32_t address) {
    if (net_driver_type == 1) {
        e1000_set_ip_address(address);
    } else if (net_driver_type == 0) {
        net_set_ip_address(address);
    }
}

uint32_t sys_net_get_ip_address(void) {
    if (net_driver_type == 1) return e1000_get_ip_address();
    if (net_driver_type == 0) return net_get_ip_address();
    return 0;
}

void sys_net_get_diagnostics(net_diagnostics_t* d) {
    if (net_driver_type == 1) {
        e1000_get_diagnostics(d);
    } else if (net_driver_type == 0) {
        net_get_diagnostics(d);
    }
}

void sys_net_initialize_stack(void) {
    uint64_t mac_value = sys_net_get_mac_address();
    uint8_t mac[6];
    for (int i = 5; i >= 0; i--) {
        mac[i] = (uint8_t)mac_value;
        mac_value >>= 8;
    }

    ipv4_init(0, mac, 0, 0);
    arp_init(0, mac);
    icmp_init();
    udp_init();
}

void sys_net_configure_address(void) {
    dhcp_lease_t lease;
    if (!dhcp_request(&lease, 100000))
        ipv4_set_configuration(0x0A0000E3, 0x0A000002, 0xFFFFFF00);
}

void sys_net_get_configuration(uint32_t* subnet_mask, uint32_t* gateway,
                               uint8_t* dhcp_leased) {
    const dhcp_lease_t* lease = dhcp_get_lease();
    if (subnet_mask) *subnet_mask = lease->leased ? lease->subnet_mask : 0xFFFFFF00;
    if (gateway) *gateway = lease->leased ? lease->gateway : 0x0A000002;
    if (dhcp_leased) *dhcp_leased = lease->leased;
}

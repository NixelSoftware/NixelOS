#include "mutex.h"
#include "kernel_net.h"
#include "drivers/net.h"
#include "drivers/net_e1000.h"
#include "drivers/net_tg3.h"
#include "drivers/net_b44.h"
#include "drivers/serial.h"
#include "net/arp.h"
#include "net/dhcp.h"
#include "net/icmp.h"
#include "net/ipv4.h"
#include "net/udp.h"
#include "net/tcp.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    const char* name;
    uint32_t driver_id;
} pci_net_match_t;


static const pci_net_match_t supported_cards[] = {
    
    // AMD PCNet-PCI II nad PCNet Fast III
    { 0x1022, 0x2000, "AMD PCNet-PCI II (Am79C970A)", 0 },
    { 0x1022, 0x2001, "AMD PCNet-FAST III (Am79C973)", 0 },

    // Broadcom B44
    { 0x14E4, 0x4401, "Broadcom BCM4401 Fast Ethernet", 3 },
    { 0x14E4, 0x170C, "Broadcom BCM4401-B0 Fast Ethernet", 3 },
    { 0x14E4, 0x4402, "Broadcom BCM4401-B1 Fast Ethernet", 3 },

    // Intel E1000 Family
    { 0x8086, 0x100E, "Intel 82540EM Gigabit Ethernet", 1 },
    { 0x8086, 0x100F, "Intel 82545EM Gigabit Ethernet", 1 },
    { 0x8086, 0x1004, "Intel 82543GC Gigabit Ethernet", 1 },
    { 0x8086, 0x1000, "Intel 82542 Gigabit Ethernet",    1 },
    { 0x8086, 0x10D3, "Intel 82574L Gigabit Ethernet",   1 },

    // zainabm: the tigon3 driver is broken, plz fix
    // zainabm: im actually so fucking done with this, fuck you and your nics broadcom
    // Broadcom Tigon3 / NetXtreme Family
    /*
    { 0x14E4, 0x1659, "Broadcom BCM5721 NetXtreme Gigabit", 2 },
    { 0x14E4, 0x165A, "Broadcom BCM5722 NetXtreme Gigabit", 2 },
    { 0x14E4, 0x1673, "Broadcom BCM57780 Gigabit Ethernet", 2 },
    { 0x14E4, 0x1677, "Broadcom BCM5751 NetXtreme Gigabit", 2 },
    { 0x14E4, 0x167A, "Broadcom BCM5754 NetXtreme Gigabit", 2 },
    { 0x14E4, 0x167B, "Broadcom BCM5755 NetXtreme Gigabit", 2 },
    { 0x14E4, 0x1690, "Broadcom BCM57780 NetLink Gigabit", 2 },
    { 0x14E4, 0x1691, "Broadcom BCM57788 NetLink Gigabit", 2 },
    { 0x14E4, 0x1692, "Broadcom BCM57781 NetLink Gigabit", 2 },
    { 0x14E4, 0x16B0, "Broadcom BCM57760 NetXtreme Gigabit", 2 },
    { 0x14E4, 0x16B1, "Broadcom BCM57785 NetLink Gigabit", 2 },
    { 0x14E4, 0x16B4, "Broadcom BCM57765 NetXtreme Gigabit", 2 },
    { 0x14E4, 0x16B5, "Broadcom BCM57786 NetLink Gigabit", 2 },
     */

    { 0, 0, 0, 0 } 
};

uint32_t net_driver_type = 0xFFFFFFFF;
uint16_t net_detected_vendor = 0;
uint16_t net_detected_device = 0;
const char* net_detected_name = "None";

static uint32_t pci_read_cfg(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void sys_net_get_device_info(uint16_t* vendor_out, uint16_t* device_out, const char** name_out) {
    MUTEX_GUARD(kernel_mutex);
    if (vendor_out) *vendor_out = net_detected_vendor;
    if (device_out) *device_out = net_detected_device;
    if (name_out)   *name_out   = net_detected_name;
}

uint8_t sys_net_init(uint16_t io) {
    MUTEX_GUARD(kernel_mutex);
    (void)io;
    net_driver_type     = 0xFFFFFFFF;
    net_detected_vendor = 0;
    net_detected_device = 0;
    net_detected_name   = "None";

    debug_print("[NET] Probing PCI bus for network adapters...\n");

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t id = pci_read_cfg((uint8_t)bus, slot, func, 0);
                uint16_t vendor = (uint16_t)id;
                uint16_t device = (uint16_t)(id >> 16);

                if (vendor == 0xFFFF || vendor == 0x0000) continue;

                
                uint32_t class_reg = pci_read_cfg((uint8_t)bus, slot, func, 0x08);
                uint8_t base_class = (uint8_t)(class_reg >> 24);

                
                if (base_class == 0x02) {
                    
                    if (net_detected_vendor == 0) {
                        net_detected_vendor = vendor;
                        net_detected_device = device;
                        net_detected_name   = "Unsupported Network Controller";
                    }

                    
                    for (int i = 0; supported_cards[i].vendor_id != 0; i++) {
                        if (supported_cards[i].vendor_id == vendor &&
                            supported_cards[i].device_id == device) {
                            debug_print("[NET] Detected: ");
                            debug_print(supported_cards[i].name);
                            debug_print("\n");

                            net_driver_type     = supported_cards[i].driver_id;
                            net_detected_vendor = vendor;
                            net_detected_device = device;
                            net_detected_name   = supported_cards[i].name;

                            goto adapter_matched;
                        }
                    }
                }
            }
        }
    }

adapter_matched:
    if (net_driver_type == 3) {
        return b44_init();
    } else if (net_driver_type == 2) {
        return tg3_init();
    } else if (net_driver_type == 1) {
        return e1000_init();
    } else if (net_driver_type == 0) {
        return net_init(0);
    }

    if (net_detected_vendor != 0) {
        debug_print("[NET] Found unsupported network card: ");
        debug_hex(net_detected_vendor);
        debug_print(":");
        debug_hex(net_detected_device);
        debug_print("\n");
    } else {
        debug_print("[NET] Warning: No network card found on PCI.\n");
    }
    return 0;
}

uint8_t sys_net_is_ready(void) {
    MUTEX_GUARD(kernel_mutex);
    if (net_driver_type == 3) return b44_is_ready();
    if (net_driver_type == 2) return tg3_is_ready();
    if (net_driver_type == 1) return e1000_is_ready();
    if (net_driver_type == 0) return net_is_ready();
    return 0;
}

void sys_net_shutdown(void) {
    MUTEX_GUARD(kernel_mutex);
    if (net_driver_type == 3) b44_shutdown();
    else if (net_driver_type == 2) tg3_shutdown();
    else if (net_driver_type == 1) e1000_shutdown();
    else if (net_driver_type == 0) net_shutdown();
}

void sys_net_poll(void) {
    MUTEX_GUARD(kernel_mutex);
    if (net_driver_type == 3) b44_poll();
    else if (net_driver_type == 2) tg3_poll();
    else if (net_driver_type == 1) e1000_poll();
    else if (net_driver_type == 0) net_poll();
    ipv4_poll();
}

uint8_t sys_net_send(const uint8_t* data, uint32_t length) {
    MUTEX_GUARD(kernel_mutex);
    if (net_driver_type == 3) return b44_send(data, length);
    if (net_driver_type == 2) return tg3_send(data, length);
    if (net_driver_type == 1) return e1000_send(data, length);
    if (net_driver_type == 0) return net_send(data, length);
    return 0;
}

void sys_net_set_receive_handler(net_receive_handler_t handler) {
    MUTEX_GUARD(kernel_mutex);
    if (net_driver_type == 3) b44_set_receive_handler(handler);
    else if (net_driver_type == 2) tg3_set_receive_handler(handler);
    else if (net_driver_type == 1) e1000_set_receive_handler(handler);
    else if (net_driver_type == 0) net_set_receive_handler(handler);
}

uint64_t sys_net_get_mac_address(void) {
    MUTEX_GUARD(kernel_mutex);
    if (net_driver_type == 3) return b44_get_mac_address();
    if (net_driver_type == 2) return tg3_get_mac_address();
    if (net_driver_type == 1) return e1000_get_mac_address();
    if (net_driver_type == 0) return net_get_mac_address();
    return 0;
}

void sys_net_set_ip_address(uint32_t address) {
    MUTEX_GUARD(kernel_mutex);
    if (net_driver_type == 3) b44_set_ip_address(address);
    else if (net_driver_type == 2) tg3_set_ip_address(address);
    else if (net_driver_type == 1) e1000_set_ip_address(address);
    else if (net_driver_type == 0) net_set_ip_address(address);
}

uint32_t sys_net_get_ip_address(void) {
    MUTEX_GUARD(kernel_mutex);
    if (net_driver_type == 3) return b44_get_ip_address();
    if (net_driver_type == 2) return tg3_get_ip_address();
    if (net_driver_type == 1) return e1000_get_ip_address();
    if (net_driver_type == 0) return net_get_ip_address();
    return 0;
}

void sys_net_get_diagnostics(net_diagnostics_t* d) {
    MUTEX_GUARD(kernel_mutex);
    if (!d) return;
    *d = (net_diagnostics_t){0};
    if (net_driver_type == 3) b44_get_diagnostics(d);
    else if (net_driver_type == 2) tg3_get_diagnostics(d);
    else if (net_driver_type == 1) e1000_get_diagnostics(d);
    else if (net_driver_type == 0) net_get_diagnostics(d);
    else d->init_failure = NET_INIT_FAILURE_DISCOVERY;
}

void sys_net_initialize_stack(void) {
    MUTEX_GUARD(kernel_mutex);
    debug_subsystem_print("[NET TRACE] reading adapter MAC\n");
    uint64_t mac_value = sys_net_get_mac_address();
    uint8_t mac[6];
    for (int i = 5; i >= 0; i--) {
        mac[i] = (uint8_t)mac_value;
        mac_value >>= 8;
    }

    debug_subsystem_print("[NET TRACE] before ipv4_init\n");
    ipv4_init(0, mac, 0, 0);
    debug_subsystem_print("[NET TRACE] after ipv4_init\n");
    debug_subsystem_print("[NET TRACE] before arp_init\n");
    arp_init(0, mac);
    debug_subsystem_print("[NET TRACE] after arp_init\n");
    debug_subsystem_print("[NET TRACE] before icmp_init\n");
    icmp_init();
    debug_subsystem_print("[NET TRACE] after icmp_init\n");
    debug_subsystem_print("[NET TRACE] before udp_init\n");
    udp_init();
    debug_subsystem_print("[NET TRACE] after udp_init\n");
    tcp_init();
}

void sys_net_configure_address(void) {
    MUTEX_GUARD(kernel_mutex);
    dhcp_lease_t lease;
    if (!dhcp_request(&lease, 100000))
        ipv4_set_configuration(0x0A0000E3, 0x0A000002, 0xFFFFFF00);
}

void sys_net_get_configuration(uint32_t* subnet_mask, uint32_t* gateway,
                               uint8_t* dhcp_leased) {
    MUTEX_GUARD(kernel_mutex);
    const dhcp_lease_t* lease = dhcp_get_lease();
    if (subnet_mask) *subnet_mask = lease->leased ? lease->subnet_mask : 0xFFFFFF00;
    if (gateway) *gateway = lease->leased ? lease->gateway : 0x0A000002;
    if (dhcp_leased) *dhcp_leased = lease->leased;
}

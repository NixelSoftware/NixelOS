#include "../mutex.h"
#include "net.h"
#include "../net/amd/pcnet.h"
#include "../net/intel/e1000.h"
#include "../net/broadcom/tg3.h"
#include "../net/broadcom/b44.h"
#include "../net/realtek/r8169.h"
#include "../drivers/serial.h"
#include "../drivers/pci.h"
#include "../net/arp.h"
#include "../net/dhcp.h"
#include "../net/icmp.h"
#include "../net/ipv4.h"
#include "../net/udp.h"
#include "../net/tcp.h"

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

    // Realtek RTL8169/8168/8111/810x family
    { 0x10EC, 0x8169, "Realtek RTL8169 Gigabit Ethernet", 4 },
    { 0x10EC, 0x8168, "Realtek RTL8168/8111 Gigabit Ethernet", 4 },
    { 0x10EC, 0x8167, "Realtek RTL8167 Gigabit Ethernet", 4 },
    { 0x10EC, 0x8162, "Realtek RTL8162 Gigabit Ethernet", 4 },
    { 0x10EC, 0x8161, "Realtek RTL8161 Gigabit Ethernet", 4 },
    { 0x10EC, 0x8136, "Realtek RTL810xE Fast Ethernet", 4 },
    { 0x10EC, 0x8129, "Realtek RTL8129 Fast Ethernet", 4 },
    { 0x10EC, 0x2502, "Realtek RTL8169S Gigabit Ethernet", 4 },
    { 0x10EC, 0x2600, "Realtek RTL8169SB Gigabit Ethernet", 4 },
    { 0x10EC, 0x0E10, "Realtek RTL8168 Gigabit Ethernet", 4 },
    { 0x1186, 0x4300, "D-Link DGE-528T Gigabit Ethernet", 4 },
    { 0x1186, 0x4302, "D-Link DGE-528T Gigabit Ethernet", 4 },

    // zainabm: the tigon3 driver is broken, plz fix
    // zainabm: im actually so fucking done with this, fuck you and your nics broadcom
    // Broadcom Tigon3 / NetXtreme Family
    { 0x14E4, 0x1659, "Broadcom BCM5721 NetXtreme Gigabit", 2 },
    { 0x14E4, 0x165A, "Broadcom BCM5722 NetXtreme Gigabit", 2 },
    { 0x14E4, 0x1673, "Broadcom BCM57780 Gigabit Ethernet", 2 },
    { 0x14E4, 0x1677, "Broadcom BCM5751 NetXtreme Gigabit", 2 },
    { 0x14E4, 0x167A, "Broadcom BCM5754 NetXtreme Gigabit", 2 },
    { 0x14E4, 0x167B, "Broadcom BCM5755 NetXtreme Gigabit", 2 },
    { 0x14E4, 0x1690, "Broadcom BCM57780 NetLink Gigabit", 2 },
    { 0x14E4, 0x1691, "Broadcom BCM57788 NetLink Gigabit", 2 },
    { 0x14E4, 0x1692, "Broadcom BCM57780 NetLink Gigabit", 2 },
    { 0x14E4, 0x16B0, "Broadcom BCM57760 NetXtreme Gigabit", 2 },
    { 0x14E4, 0x16B1, "Broadcom BCM57785 NetLink Gigabit", 2 },
    { 0x14E4, 0x16B4, "Broadcom BCM57765 NetXtreme Gigabit", 2 },
    { 0x14E4, 0x16B5, "Broadcom BCM57786 NetLink Gigabit", 2 },

    { 0, 0, 0, 0 } 
};

uint32_t net_driver_type = 0xFFFFFFFF;
uint16_t net_detected_vendor = 0;
uint16_t net_detected_device = 0;
const char* net_detected_name = "None";
static const pci_device_t* selected_pci_device;

const pci_device_t* sys_net_pci_device(void) { return selected_pci_device; }

void sys_net_get_device_info(uint16_t* vendor_out, uint16_t* device_out, const char** name_out) {
    MUTEX_GUARD(kernel_mutex);
    if (vendor_out) *vendor_out = net_detected_vendor;
    if (device_out) *device_out = net_detected_device;
    if (name_out)   *name_out   = net_detected_name;
}

uint8_t sys_net_init(uint16_t io) {
    MUTEX_GUARD(kernel_mutex);
    (void)io;
    selected_pci_device = 0;
    net_driver_type     = 0xFFFFFFFF;
    net_detected_vendor = 0;
    net_detected_device = 0;
    net_detected_name   = "None";

    debug_print("[NET] Probing PCI bus for network adapters...\n");

    if (!pci_init()) debug_print("[PCI] Device inventory exceeds capacity\n");
    for (uint32_t index = 0; index < pci_device_count(); index++) {
        const pci_device_t* dev = pci_device_at(index);
        if (dev->class_code != 0x02) continue;
        if (!net_detected_vendor) {
            net_detected_vendor = dev->vendor_id;
            net_detected_device = dev->device_id;
            net_detected_name = "Unsupported Network Controller";
        }
        for (uint32_t i = 0; supported_cards[i].vendor_id; i++) {
            if (supported_cards[i].vendor_id != dev->vendor_id ||
                supported_cards[i].device_id != dev->device_id) continue;
            net_driver_type = supported_cards[i].driver_id;
            net_detected_vendor = dev->vendor_id;
            net_detected_device = dev->device_id;
            net_detected_name = supported_cards[i].name;
            selected_pci_device = dev;
            debug_print("[NET] Detected: ");
            debug_print(net_detected_name);
            debug_print("\n");
            goto adapter_matched;
        }
    }

adapter_matched:
    if (net_driver_type == 4) {
        return r8169_init();
    } else if (net_driver_type == 3) {
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
    if (net_driver_type == 4) return r8169_is_ready();
    if (net_driver_type == 3) return b44_is_ready();
    if (net_driver_type == 2) return tg3_is_ready();
    if (net_driver_type == 1) return e1000_is_ready();
    if (net_driver_type == 0) return net_is_ready();
    return 0;
}

void sys_net_shutdown(void) {
    MUTEX_GUARD(kernel_mutex);
    if (net_driver_type == 4) r8169_shutdown();
    else if (net_driver_type == 3) b44_shutdown();
    else if (net_driver_type == 2) tg3_shutdown();
    else if (net_driver_type == 1) e1000_shutdown();
    else if (net_driver_type == 0) net_shutdown();
    dhcp_clear_lease();
    ipv4_set_configuration(0, 0, 0);
}

void sys_net_poll(void) {
    MUTEX_GUARD(kernel_mutex);
    if (net_driver_type == 4) r8169_poll();
    else if (net_driver_type == 3) b44_poll();
    else if (net_driver_type == 2) tg3_poll();
    else if (net_driver_type == 1) e1000_poll();
    else if (net_driver_type == 0) net_poll();
    ipv4_poll();
}

uint8_t sys_net_send(const uint8_t* data, uint32_t length) {
    MUTEX_GUARD(kernel_mutex);
    if (net_driver_type == 4) return r8169_send(data, length);
    if (net_driver_type == 3) return b44_send(data, length);
    if (net_driver_type == 2) return tg3_send(data, length);
    if (net_driver_type == 1) return e1000_send(data, length);
    if (net_driver_type == 0) return net_send(data, length);
    return 0;
}

void sys_net_set_receive_handler(net_receive_handler_t handler) {
    MUTEX_GUARD(kernel_mutex);
    if (net_driver_type == 4) r8169_set_receive_handler(handler);
    else if (net_driver_type == 3) b44_set_receive_handler(handler);
    else if (net_driver_type == 2) tg3_set_receive_handler(handler);
    else if (net_driver_type == 1) e1000_set_receive_handler(handler);
    else if (net_driver_type == 0) net_set_receive_handler(handler);
}

uint64_t sys_net_get_mac_address(void) {
    MUTEX_GUARD(kernel_mutex);
    if (net_driver_type == 4) return r8169_get_mac_address();
    if (net_driver_type == 3) return b44_get_mac_address();
    if (net_driver_type == 2) return tg3_get_mac_address();
    if (net_driver_type == 1) return e1000_get_mac_address();
    if (net_driver_type == 0) return net_get_mac_address();
    return 0;
}

void sys_net_set_ip_address(uint32_t address) {
    MUTEX_GUARD(kernel_mutex);
    if (net_driver_type == 4) r8169_set_ip_address(address);
    else if (net_driver_type == 3) b44_set_ip_address(address);
    else if (net_driver_type == 2) tg3_set_ip_address(address);
    else if (net_driver_type == 1) e1000_set_ip_address(address);
    else if (net_driver_type == 0) net_set_ip_address(address);
}

uint32_t sys_net_get_ip_address(void) {
    MUTEX_GUARD(kernel_mutex);
    if (net_driver_type == 4) return r8169_get_ip_address();
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
    if (net_driver_type == 4) r8169_get_diagnostics(d);
    else if (net_driver_type == 3) b44_get_diagnostics(d);
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
    /* dhcp_request owns short critical sections, never the whole wait. */
    dhcp_lease_t lease;
    if (!dhcp_request(&lease, 4)) {
        debug_print("[DHCP] No lease; configure an address in Settings\n");
    } else {
        debug_print("[DHCP] Lease acquired: ");
        debug_hex(lease.address);
        debug_print("\n");
    }
}

void sys_net_get_configuration(uint32_t* subnet_mask, uint32_t* gateway,
                               uint8_t* dhcp_leased) {
    MUTEX_GUARD(kernel_mutex);
    ipv4_get_configuration(subnet_mask, gateway);
    if (dhcp_leased) *dhcp_leased = dhcp_get_lease()->leased;
}

uint8_t sys_net_set_manual_configuration(uint32_t address, uint32_t mask, uint32_t gateway) {
    MUTEX_GUARD(kernel_mutex);
    uint32_t host = ~mask;
    if (!sys_net_is_ready() || !address || (address >> 24) == 0 ||
        (address >> 24) == 127 || (address >> 24) >= 224 || !mask ||
        (host & (host + 1)) || host < 3 || !(address & host) ||
        (address & host) == host ||
        (gateway && ((gateway & mask) != (address & mask) ||
         !(gateway & host) || (gateway & host) == host || gateway == address))) return 0;
    dhcp_clear_lease();
    ipv4_set_configuration(address, gateway, mask);
    return 1;
}

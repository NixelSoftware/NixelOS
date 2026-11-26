#ifndef KERNEL_HAL_NET_H
#define KERNEL_HAL_NET_H

#include "../types.h"
#include "net_types.h"
#include "../drivers/pci.h"

/* PCI function for the primary stack, or the adapter being initialized. */
const pci_device_t* sys_net_pci_device(void);

// 0 = PCNet32, 1 = Intel E1000, 2 = Tigon3, 3 = BCM4401,
// 4 = Realtek RTL8169/8168, 5 = Intel 3945 Wi-Fi; ~0u = none
extern uint32_t net_driver_type;

void sys_net_get_device_info(uint16_t* vendor_out, uint16_t* device_out, const char** name_out);
uint8_t  sys_net_init(uint16_t io);
uint8_t  sys_net_is_ready(void);
void     sys_net_shutdown(void);
void     sys_net_poll(void);
/* Network task only: sleep until there is something for sys_net_poll(). */
void     sys_net_wait(void);
uint8_t  sys_net_send(const uint8_t* data, uint32_t length);
void     sys_net_set_receive_handler(net_receive_handler_t handler);
uint64_t sys_net_get_mac_address(void);
void     sys_net_set_ip_address(uint32_t address);
uint32_t sys_net_get_ip_address(void);
void     sys_net_get_diagnostics(net_diagnostics_t* d);
/* Both links can stay up. Existing IPv4 clients use the primary link;
 * link-specific frame sends do not change the primary stack. */
typedef enum { SYS_NET_ETHERNET, SYS_NET_WIFI } sys_net_link_t;
uint8_t sys_net_link_is_ready(sys_net_link_t link);
uint8_t sys_net_link_send(sys_net_link_t link, const uint8_t* data, uint32_t length);
/* Optional raw receive callback, independent of the primary stack handler. */
void sys_net_link_set_receive_handler(sys_net_link_t link, net_receive_handler_t handler);
uint64_t sys_net_link_mac(sys_net_link_t link);
uint32_t sys_net_link_ip(sys_net_link_t link);
uint8_t sys_net_select_wifi(void);
void sys_net_wifi_down(void);
void     sys_net_initialize_stack(void);
void     sys_net_configure_address(void);
uint8_t sys_net_set_manual_configuration(uint32_t address, uint32_t mask, uint32_t gateway);
void     sys_net_get_configuration(uint32_t* subnet_mask, uint32_t* gateway,
                                   uint8_t* dhcp_leased);

#endif

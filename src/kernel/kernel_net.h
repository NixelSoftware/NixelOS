#ifndef KERNEL_NET_H
#define KERNEL_NET_H

#include "types.h"
#include "drivers/net.h"

// 0 = PCNet32 (default), 1 = Intel E1000
extern uint32_t net_driver_type;

uint8_t  sys_net_init(uint16_t io);
uint8_t  sys_net_is_ready(void);
void     sys_net_shutdown(void);
void     sys_net_poll(void);
uint8_t  sys_net_send(const uint8_t* data, uint32_t length);
void     sys_net_set_receive_handler(net_receive_handler_t handler);
uint64_t sys_net_get_mac_address(void);
void     sys_net_set_ip_address(uint32_t address);
uint32_t sys_net_get_ip_address(void);
void     sys_net_get_diagnostics(net_diagnostics_t* d);

#endif
#ifndef NET_E1000_H
#define NET_E1000_H

#include "../../types.h"
#include "../../hal/net_types.h"

uint8_t  e1000_init(void);
uint8_t  e1000_is_ready(void);
void     e1000_shutdown(void);
void     e1000_poll(void);
uint8_t  e1000_send(const uint8_t* data, uint32_t length);
void     e1000_set_receive_handler(net_receive_handler_t handler);
uint64_t e1000_get_mac_address(void);
void     e1000_set_ip_address(uint32_t address);
uint32_t e1000_get_ip_address(void);
void     e1000_get_diagnostics(net_diagnostics_t* diagnostics);

#endif
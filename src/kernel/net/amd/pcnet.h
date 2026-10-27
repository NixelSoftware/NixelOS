#ifndef AMD_PCNET_H
#define AMD_PCNET_H

#include "../../hal/net_types.h"

uint8_t net_init(uint16_t io_base);
uint8_t net_is_ready(void);
void net_shutdown(void);
void net_poll(void);

uint8_t net_send(const uint8_t* data, uint32_t length);
void net_set_receive_handler(net_receive_handler_t handler);

uint64_t net_get_mac_address(void);
void net_set_ip_address(uint32_t address);
uint32_t net_get_ip_address(void);
void net_get_diagnostics(net_diagnostics_t* diagnostics);
void net_reset_diagnostics(void);

#endif

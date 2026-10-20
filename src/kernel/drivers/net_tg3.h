#ifndef NET_TG3_H
#define NET_TG3_H

#include "../types.h"
#include "net.h"

uint8_t  tg3_init(void);
uint8_t  tg3_is_ready(void);
void     tg3_shutdown(void);
void     tg3_poll(void);
uint8_t  tg3_send(const uint8_t* data, uint32_t length);
void     tg3_set_receive_handler(net_receive_handler_t handler);
uint64_t tg3_get_mac_address(void);
void     tg3_set_ip_address(uint32_t address);
uint32_t tg3_get_ip_address(void);
void     tg3_get_diagnostics(net_diagnostics_t* diagnostics);

#endif
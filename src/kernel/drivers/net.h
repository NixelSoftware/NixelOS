#ifndef NET_H
#define NET_H

#include "../types.h"

#define NET_MAX_FRAME_SIZE 1518

typedef uint8_t (*net_receive_handler_t)(const uint8_t* data, uint32_t length);


uint8_t net_init(uint16_t io_base);
uint8_t net_is_ready(void);
void net_shutdown(void);
void net_poll(void);

uint8_t net_send(const uint8_t* data, uint32_t length);
void net_set_receive_handler(net_receive_handler_t handler);

uint64_t net_get_mac_address(void);
void net_set_ip_address(uint32_t address);
uint32_t net_get_ip_address(void);

#endif
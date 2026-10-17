#ifndef UDP_H
#define UDP_H

#include "../types.h"
#include "ipv4.h"

#define UDP_PROTOCOL 17
#define UDP_MAX_SOCKETS 32
#define UDP_HEADER_SIZE 8
#define UDP_MAX_PAYLOAD (IPV4_MAX_PAYLOAD - UDP_HEADER_SIZE)

typedef struct udp_socket udp_socket_t;
typedef void (*udp_data_handler_t)(udp_socket_t* socket,
                                   uint32_t source_ip,
                                   uint16_t source_port,
                                   const uint8_t* data,
                                   uint16_t length);

void udp_init(void);
udp_socket_t* udp_connect(uint32_t remote_ip, uint16_t remote_port);
udp_socket_t* udp_listen(uint16_t local_port);
void udp_bind(udp_socket_t* socket, udp_data_handler_t handler);
uint8_t udp_send(udp_socket_t* socket, const uint8_t* data, uint16_t length);
void udp_disconnect(udp_socket_t* socket);

uint16_t udp_get_local_port(const udp_socket_t* socket);
uint16_t udp_get_remote_port(const udp_socket_t* socket);
uint32_t udp_get_remote_ip(const udp_socket_t* socket);

#endif

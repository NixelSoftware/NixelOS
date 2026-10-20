#ifndef TCP_H
#define TCP_H

#include "../types.h"

#define TCP_PROTOCOL 6
#define TCP_MAX_SOCKETS 32
#define TCP_MAX_PAYLOAD 1448

typedef enum {
    TCP_CLOSED,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT1,
    TCP_FIN_WAIT2,
    TCP_CLOSE_WAIT,
    TCP_TIME_WAIT
} tcp_socket_state_t;

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20
#define TCP_FLAG_ECE 0x40
#define TCP_FLAG_CWR 0x80

typedef struct tcp_socket tcp_socket_t;
typedef uint8_t (*tcp_data_handler_t)(tcp_socket_t* socket,
                                      const uint8_t* data,
                                      uint16_t length);

void tcp_init(void);
tcp_socket_t* tcp_connect(uint32_t remote_ip, uint16_t remote_port);
tcp_socket_t* tcp_listen(uint16_t local_port);
void tcp_bind(tcp_socket_t* socket, tcp_data_handler_t handler);
uint8_t tcp_send(tcp_socket_t* socket, const uint8_t* data, uint16_t length);
void tcp_disconnect(tcp_socket_t* socket);
void tcp_release(tcp_socket_t* socket);
tcp_socket_state_t tcp_get_state(const tcp_socket_t* socket);

#endif

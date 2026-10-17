#ifndef NET_H
#define NET_H

#include "../types.h"

#define NET_MAX_FRAME_SIZE 1518

typedef uint8_t (*net_receive_handler_t)(const uint8_t* data, uint32_t length);

typedef struct {
    uint8_t initialized;
    uint16_t io_base;
    uint16_t csr0;
    uint8_t tx_index;
    uint8_t rx_index;
    uint32_t tx_attempts;
    uint32_t tx_submitted;
    uint32_t tx_busy;
    uint32_t rx_frames;
    uint32_t rx_errors;
    uint32_t rx_invalid;
    uint32_t last_rx_length;
    uint16_t last_error_status;
} net_diagnostics_t;

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
#ifndef NET_H
#define NET_H

#include "../types.h"

#define NET_MAX_FRAME_SIZE 1518

/* Shared init_failure meanings for every network driver. */
typedef enum {
    NET_INIT_FAILURE_NONE = 0,
    NET_INIT_FAILURE_DISCOVERY = 1,
    NET_INIT_FAILURE_REGISTER_ACCESS = 2,
    NET_INIT_FAILURE_TIMEOUT = 3
} net_init_failure_t;

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
    uint32_t tx_completed;
    uint32_t tx_errors;
    uint32_t rx_frames;
    uint32_t rx_errors;
    uint32_t rx_invalid;
    uint32_t last_rx_length;
    uint16_t last_error_status;
    uint16_t last_tx_status;
    uint32_t last_tx_misc;
    uint32_t tx_descriptor_address;
    int16_t tx_descriptor_length;
    uint16_t tx_descriptor_status;
    uint8_t init_failure;
    uint16_t sw_csr0;
    uint32_t dw_csr0;
    uint16_t rx_descriptor_status;
    uint32_t rx_message_length;

    // phy shit
    uint8_t  phy_addr;
    uint16_t phy_id;
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

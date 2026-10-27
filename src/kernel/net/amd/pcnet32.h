#ifndef NET_PCNET32_H
#define NET_PCNET32_H

#include "../../types.h"

#define PCNET_VENDOR_ID   0x1022
#define PCNET_DEVICE_ID   0x2000

#define PCNET_RX_BUFFERS  8
#define PCNET_TX_BUFFERS  8
#define PCNET_BUF_SIZE    1536

typedef struct {
    uint16_t io_base;
    uint8_t  irq;
    uint8_t  mac[6];
} pcnet_device_t;

int  pcnet_init(uint16_t io_base, uint8_t irq);
void pcnet_send_packet(const uint8_t *buffer, uint16_t len);
void pcnet_handle_interrupt(void);
void pcnet_poll(void);
const uint8_t *pcnet_get_mac(void);

extern void net_receive_packet(const uint8_t *data, uint16_t len);

#endif
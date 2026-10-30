#ifndef NET_R8169_H
#define NET_R8169_H

#include "../../types.h"
#include "../../hal/net_types.h"

/* Realtek RTL8169/8110/8168/810x/8111 PCI Gigabit/Fast Ethernet.
 * Bare-metal polled-DMA port of the register level behaviour described by
 * Linux drivers/net/ethernet/realtek/r8169_main.c (GPL-2.0-only). */

/* Optional boot-only observer, matching the BCM4401 driver. */
void r8169_set_init_progress(void (*progress)(const char* status));

uint8_t  r8169_init(void);
uint8_t  r8169_is_ready(void);
void     r8169_shutdown(void);
void     r8169_poll(void);
uint8_t  r8169_send(const uint8_t* data, uint32_t length);
void     r8169_set_receive_handler(net_receive_handler_t handler);
uint64_t r8169_get_mac_address(void);
void     r8169_set_ip_address(uint32_t address);
uint32_t r8169_get_ip_address(void);
void     r8169_get_diagnostics(net_diagnostics_t* output);

#endif

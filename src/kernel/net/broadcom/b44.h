#ifndef NET_B44_H
#define NET_B44_H
#include "../../hal/net_types.h"
/* Optional boot-only observer; unset after initialization. */
void b44_set_init_progress(void (*progress)(const char* status));
uint8_t b44_init(void);
uint8_t b44_is_ready(void);
void b44_shutdown(void);
void b44_poll(void);
uint8_t b44_send(const uint8_t* data, uint32_t length);
void b44_set_receive_handler(net_receive_handler_t handler);
uint64_t b44_get_mac_address(void);
void b44_set_ip_address(uint32_t address);
uint32_t b44_get_ip_address(void);
void b44_get_diagnostics(net_diagnostics_t* output);
#endif

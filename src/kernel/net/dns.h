#ifndef DNS_H
#define DNS_H

#include "../types.h"

uint8_t dns_resolve(const char* hostname, uint32_t* address);
uint8_t dns_resolve_async_start(const char* hostname);
int dns_resolve_async_poll(uint32_t* address);
void dns_resolve_async_cancel(void);

#endif

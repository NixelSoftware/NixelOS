#ifndef DNS_H
#define DNS_H

#include "../types.h"

uint8_t dns_resolve(const char* hostname, uint32_t* address);

#endif
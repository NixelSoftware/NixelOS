#ifndef DHCP_H
#define DHCP_H

#include "../types.h"

typedef struct {
    uint8_t leased;
    uint32_t address;
    uint32_t subnet_mask;
    uint32_t gateway;
    uint32_t dns;
    uint32_t server;
    uint32_t lease_seconds;
} dhcp_lease_t;

uint8_t dhcp_request(dhcp_lease_t* lease, uint32_t wait_iterations);
const dhcp_lease_t* dhcp_get_lease(void);

#endif

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

/* Timeout in RTC seconds per attempt; each exchange tries up to three times. */
uint8_t dhcp_request(dhcp_lease_t* lease, uint32_t timeout_seconds);
const dhcp_lease_t* dhcp_get_lease(void);
void dhcp_clear_lease(void);

#endif

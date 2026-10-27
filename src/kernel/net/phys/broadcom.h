#ifndef NIXEL_PHY_BROADCOM_H
#define NIXEL_PHY_BROADCOM_H
#include "../../types.h"
#define BCM57780_PHY_ID 0x03625D90u
#define BCM_PHY_MODEL_MASK 0xFFFFFFF0u
/* Caller serializes the MDIO bus across selector/data operations.
 * Callbacks return nonzero on success, zero on timeout/hardware failure. */
typedef struct {
    void* context;
    int (*read)(void* context, uint8_t reg, uint16_t* value);
    int (*write)(void* context, uint8_t reg, uint16_t value);
} bcm_mdio_t;
/* After BMCR reset, before restarting autonegotiation. No IRQs, heap, Linux
 * PHY state machine, or workqueue are required by the polling driver. */
int bcm57780_phy_configure(const bcm_mdio_t* bus, uint32_t phy_id);
#endif

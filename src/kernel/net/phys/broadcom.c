/* SPDX-License-Identifier: GPL-2.0-or-later
 * BCM57780 PHY initialization adapted from the supplied Linux broadcom.c,
 * bcm54xx_config_init / bcm54xx_adjust_rxrefclk / bcm54xx_phydsp_config.
 * Original Broadcom PHY driver Copyright (c) 2006 Maciej W. Rozycki.
 * Native Nixel MDIO callbacks replace Linux phy_device and PHYLIB calls.
 */
#include "broadcom.h"

#define PHY_ECR        0x10
#define PHY_EXP_DATA   0x15
#define PHY_EXP_SELECT 0x17
#define PHY_AUX_CTL    0x18
#define PHY_ISR        0x1A
#define PHY_IMR        0x1B
#define PHY_SHADOW     0x1C
#define ECR_MASK_IRQS  0x1000
#define SHADOW_WRITE   0x8000
#define SHADOW_DATA    0x03FF
#define SCR3           0x05
#define SCR3_DLLAPD_DIS 0x0002
#define APD            0x0A
#define APD_ENABLE     0x0020
#define AUX_TX_6DB     0x0400
#define AUX_SMDSP_EN   0x0800
#define EXP75          0x0F75
#define EXP75_CM_OSC   0x0001
#define WOL_INT_STATUS 0x0E94

static int shadow_read(const bcm_mdio_t* b, uint8_t selector, uint16_t* value) {
    if (!b->write(b->context, PHY_SHADOW, (uint16_t)selector << 10) ||
        !b->read(b->context, PHY_SHADOW, value)) return 0;
    *value &= SHADOW_DATA;
    return 1;
}
static int shadow_write(const bcm_mdio_t* b, uint8_t selector, uint16_t value) {
    return b->write(b->context, PHY_SHADOW,
                    SHADOW_WRITE | ((uint16_t)selector << 10) | (value & SHADOW_DATA));
}
static int expansion_read(const bcm_mdio_t* b, uint16_t selector, uint16_t* value) {
    if (!b->write(b->context, PHY_EXP_SELECT, selector)) return 0;
    int ok = b->read(b->context, PHY_EXP_DATA, value);
    /* Always restore the default expansion selector, including read failure. */
    int restored = b->write(b->context, PHY_EXP_SELECT, 0);
    return ok && restored;
}
static int expansion_write(const bcm_mdio_t* b, uint16_t selector, uint16_t value) {
    return b->write(b->context, PHY_EXP_SELECT, selector) &&
           b->write(b->context, PHY_EXP_DATA, value);
}
int bcm57780_phy_configure(const bcm_mdio_t* b, uint32_t phy_id) {
    if (!b || !b->read || !b->write || (phy_id & BCM_PHY_MODEL_MASK) != BCM57780_PHY_ID)
        return 0;
    uint16_t value;
    if (!b->read(b->context, PHY_ECR, &value) ||
        !b->write(b->context, PHY_ECR, value | ECR_MASK_IRQS) ||
        !b->write(b->context, PHY_IMR, (uint16_t)~0x000Eu)) return 0;
    /* Tg3 selects GMII and AUTO_PWRDWN_ENABLE for this PHY. Preserve the
     * 125 MHz reference clock while enabling DLL/PHY automatic power-down. */
    if (!shadow_read(b, SCR3, &value) ||
        !shadow_write(b, SCR3, value & ~SCR3_DLLAPD_DIS) ||
        !shadow_read(b, APD, &value) ||
        !shadow_write(b, APD, value | APD_ENABLE)) return 0;
    /* BCM57780's DSP oscillator workaround. Restore the SMDSP clock even
     * when a register operation fails; propagate every MDIO error. */
    int ok = b->write(b->context, PHY_AUX_CTL, AUX_SMDSP_EN | AUX_TX_6DB);
    if (ok) ok = expansion_read(b, EXP75, &value);
    if (ok) ok = expansion_write(b, EXP75, value | EXP75_CM_OSC);
    int disabled = b->write(b->context, PHY_AUX_CTL, AUX_TX_6DB);
    if (!ok || !disabled) return 0;
    /* Ack old interrupt state. LEDs and jumbo/PTP/WoL configuration are left
     * alone: this port uses standard-MTU copper Ethernet and polling. */
    return b->read(b->context, PHY_ISR, &value) && expansion_read(b, WOL_INT_STATUS, &value);
}

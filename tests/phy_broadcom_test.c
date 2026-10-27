/* cc -O2 -Wall -Wextra tests/phy_broadcom_test.c -o /tmp/phy_broadcom_test && /tmp/phy_broadcom_test */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/kernel/net/phys/broadcom.c"
typedef struct {
    uint16_t regs[32], shadows[32], expansion[4096], shadow, selector;
    unsigned calls, fail_at, aux_attempts, last_aux;
} model_t;
static model_t model;
static int read_reg(void* context, uint8_t reg, uint16_t* value) {
    model_t* m = context;
    if (++m->calls == m->fail_at) return 0;
    if (reg == PHY_SHADOW) *value = m->shadows[m->shadow];
    else if (reg == PHY_EXP_DATA) *value = m->expansion[m->selector];
    else *value = m->regs[reg];
    return 1;
}
static int write_reg(void* context, uint8_t reg, uint16_t value) {
    model_t* m = context;
    if (reg == PHY_AUX_CTL) { m->aux_attempts++; m->last_aux = value; }
    if (++m->calls == m->fail_at) return 0;
    if (reg == PHY_SHADOW) {
        m->shadow = (value >> 10) & 31;
        if (value & SHADOW_WRITE) m->shadows[m->shadow] = value & SHADOW_DATA;
    } else if (reg == PHY_EXP_SELECT) m->selector = value;
    else if (reg == PHY_EXP_DATA) m->expansion[m->selector] = value;
    else m->regs[reg] = value;
    return 1;
}
static void reset(void) {
    memset(&model, 0, sizeof(model));
    model.regs[PHY_ECR] = 0x21;
    model.shadows[SCR3] = 0x107;
    model.shadows[APD] = 0x104;
    model.expansion[EXP75] = 0x24;
}
int main(void) {
    const bcm_mdio_t bus = {&model, read_reg, write_reg};
    reset();
    assert(bcm57780_phy_configure(&bus, BCM57780_PHY_ID | 7));
    assert((model.regs[PHY_ECR] & 0x1021) == 0x1021);
    assert(model.regs[PHY_IMR] == 0xFFF1);
    assert(model.shadows[SCR3] == 0x105); /* Preserve CLK125, clear DLLAPD_DIS. */
    assert(model.shadows[APD] == 0x124);
    assert(model.expansion[EXP75] == 0x25);
    assert(model.regs[PHY_AUX_CTL] == AUX_TX_6DB && model.aux_attempts == 2);
    assert(!model.selector);
    unsigned operations = model.calls;
    for (unsigned failed = 1; failed <= operations; failed++) {
        reset(); model.fail_at = failed;
        assert(!bcm57780_phy_configure(&bus, BCM57780_PHY_ID));
        if (model.aux_attempts) assert(model.last_aux == AUX_TX_6DB);
    }
    reset();
    assert(!bcm57780_phy_configure(&bus, 0x0143BCA0) && !model.calls);
    assert(!bcm57780_phy_configure(0, BCM57780_PHY_ID));
    puts("PASS: BCM57780 clock/APD/DSP/interrupt configuration, PHY revision matching, every MDIO failure and DSP cleanup path");
}

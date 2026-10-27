/* cc -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections
 * -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast tests/net_tg3_phy_test.c
 * -o /tmp/net_tg3_phy_test && /tmp/net_tg3_phy_test */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define TG3_HOST_TEST
#include "../src/kernel/net/phys/broadcom.c"
#include "../src/kernel/net/broadcom/tg3.c"
static uint32_t registers[0x8000/4], requests, phy_errors, ticks;
static uint16_t mii[32], shadow[32], expansion[4096], sh, exp_sel;
void driver_delay_us(uint32_t us) { ticks += us; }
void debug_print(const char* text) { (void)text; }
void debug_hex(uint32_t value) { (void)value; }
uint32_t tg3_test_read32(uint32_t reg) { assert(reg < sizeof(registers)); return registers[reg/4]; }
void tg3_test_write_reset(uint32_t value) { (void)value; assert(0); }
void tg3_test_write32(uint32_t reg, uint32_t value) {
    assert(reg < sizeof(registers));
    registers[reg/4] = value;
    if (reg != TG3_MAC_MI_COM) return;
    requests++;
    uint8_t addr = (value >> 21) & 31, index = (value >> 16) & 31;
    uint16_t data = value;
    value &= ~MI_COM_BUSY;
    if (addr != 1 || phy_errors) { registers[reg/4] = value | MI_COM_READ_FAILED; return; }
    if (value & MI_COM_CMD_READ) {
        if (index == PHY_SHADOW) data = shadow[sh];
        else if (index == PHY_EXP_DATA) data = expansion[exp_sel];
        else data = mii[index];
    } else {
        if (index == MII_BMCR) data &= ~BMCR_RESET;
        if (index == PHY_SHADOW) {
            sh = (data >> 10) & 31;
            if (data & SHADOW_WRITE) shadow[sh] = data & SHADOW_DATA;
        } else if (index == PHY_EXP_SELECT) exp_sel = data;
        else if (index == PHY_EXP_DATA) expansion[exp_sel] = data;
        else mii[index] = data;
    }
    registers[reg/4] = (value & ~0xFFFFu) | data;
}
int main(void) {
    tg3_cpmu = 1; tg3_asic = 0x57780;
    mii[MII_PHYID1] = 0x0362; mii[MII_PHYID2] = 0x5D97;
    mii[MII_BMCR] = BMCR_PDOWN;
    shadow[SCR3] = 3;
    assert(tg3_phy_init());
    assert(tg3_phy_addr == 1 && tg3_phy_identifier == 0x03625D97);
    assert(registers[TG3_MAC_MI_MODE/4] == 0x8000);
    assert(!(mii[MII_BMCR] & BMCR_PDOWN) && (mii[MII_BMCR] & BMCR_ANENABLE));
    assert(expansion[EXP75] & EXP75_CM_OSC);
    assert(!(mii[PHY_AUX_CTL] & AUX_SMDSP_EN) && (shadow[APD] & APD_ENABLE));
    mii[MII_BMSR] = 0x24; mii[10] = 0x800;
    tg3_update_link(); assert(tg3_link_up && (registers[TG3_MAC_MODE/4] & 0xE) == 8);
    mii[10] = 0; mii[5] = 0x80;
    tg3_update_link(); assert(tg3_link_up && (registers[TG3_MAC_MODE/4] & 0xE) == 6);
    assert((registers[TG3_MAC_TX_LENGTHS/4] & 255) == 32);
    mii[5] = 0x40;
    tg3_update_link(); assert(tg3_link_up && (registers[TG3_MAC_MODE/4] & 0xE) == 4);
    mii[MII_ADVERTISE] = 0; tg3_update_link(); assert(!tg3_link_up);
    mii[MII_BMSR] = 0; tg3_update_link(); assert(!tg3_link_up);
    phy_errors = 1; tg3_update_link(); assert(!tg3_link_up);
    puts("PASS: BCM57780 native MDIO/PHY bring-up, 500kHz clock, powered-down PHY, DSP setup, negotiated speed/duplex and link failures");
}

/* cc -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections
 * -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast tests/net_tg3_reset_test.c
 * -o /tmp/net_tg3_reset_test && /tmp/net_tg3_reset_test */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define TG3_HOST_TEST
#include "../src/kernel/net/broadcom/tg3.c"
static uint32_t regs[0x8000/4], sram[0x1000/4], window;
static unsigned mmio_off, reset_count, restored, grant, acknowledge, total_delay, firmware_warnings;
static pci_device_t dev = {.vendor_id=0x14E4, .device_id=0x167B};
uint32_t tg3_test_read32(uint32_t reg) { assert(!mmio_off && reg < sizeof(regs)); return regs[reg/4]; }
void tg3_test_write32(uint32_t reg, uint32_t value) {
    assert(!mmio_off && reg < sizeof(regs));
    regs[reg/4] = value;
    if (reg == NVRAM_SWARB && value == SWARB_REQ_SET1 && grant) regs[reg/4] |= SWARB_GNT1;
    if (reg == GRC_RX_CPU_EVENT) regs[reg/4] &= ~GRC_RX_CPU_DRIVER_EVENT;
}
void tg3_test_write_reset(uint32_t value) {
    assert(!mmio_off && (value & GRC_MISC_CFG_CORECLK_RESET));
    assert(sram[NIC_SRAM_FIRMWARE_MBOX/4] == NIC_SRAM_FIRMWARE_MBOX_MAGIC1);
    mmio_off = 1; reset_count++; total_delay = 0;
    regs[TG3_MAC_ADDR_0_HIGH/4] = regs[TG3_MAC_ADDR_0_LOW/4] = 0;
}
void driver_delay_us(uint32_t us) { total_delay += us; }
void debug_print(const char* message) { if (strstr(message, "No boot firmware")) firmware_warnings++; }
void debug_hex(uint32_t value) { (void)value; }
uint8_t pci_find_capability(const pci_device_t* d, uint8_t cap) { assert(d == &dev); return cap == PCI_CAP_PCIE ? 0x50 : 0; }
uint16_t pci_read16(const pci_device_t* d, uint16_t reg) {
    assert(d == &dev);
    if (reg == 0) return d->vendor_id;
    if (reg == 4 && mmio_off) assert(total_delay >= 120);
    return 0;
}
void pci_write16(const pci_device_t* d, uint16_t reg, uint16_t value) { assert(d == &dev); (void)reg; (void)value; }
void pci_write32(const pci_device_t* d, uint16_t reg, uint32_t value) {
    assert(d == &dev);
    if (reg == TG3PCI_MEM_WIN_BASE_ADDR) window = value;
    if (reg == TG3PCI_MEM_WIN_DATA) { assert(window < sizeof(sram)); sram[window/4] = value; }
}
uint32_t pci_read32(const pci_device_t* d, uint16_t reg) {
    assert(d == &dev && reg == TG3PCI_MEM_WIN_DATA && window < sizeof(sram));
    return sram[window/4];
}
int pci_save_state(const pci_device_t* d, pci_saved_state_t* state) {
    assert(d == &dev && !mmio_off); state->valid = 1; return 1;
}
int pci_restore_state(const pci_device_t* d, const pci_saved_state_t* state) {
    assert(d == &dev && state->valid && mmio_off && total_delay >= 240);
    mmio_off = 0; restored++;
    if (acknowledge) sram[NIC_SRAM_FIRMWARE_MBOX/4] = ~(uint32_t)NIC_SRAM_FIRMWARE_MBOX_MAGIC1;
    return 1;
}
void pci_prepare_polling(const pci_device_t* d) { assert(d == &dev && !mmio_off); }
static void setup(void) {
    memset(regs, 0, sizeof(regs)); memset(sram, 0, sizeof(sram));
    tg3_pci = &dev; tg3_mmio_base = 1; tg3_asic = 0xA;
    tg3_chip_revision = 0xA000; tg3_cpmu = tg3_57765 = 0;
    tg3_mac_addr = 0x525400123456ULL; tg3_host_control = 0xA000009A;
    mmio_off = reset_count = restored = total_delay = firmware_warnings = 0;
    grant = acknowledge = 1;
}
int main(void) {
    setup();
    assert(tg3_reset_chip() && reset_count == 1 && restored == 1);
    assert(regs[TG3_MAC_ADDR_0_HIGH/4] == 0x5254 && regs[TG3_MAC_ADDR_0_LOW/4] == 0x00123456);
    assert(!window && !firmware_warnings);
    setup();
    sram[NIC_SRAM_DATA_SIG/4] = NIC_SRAM_DATA_SIG_MAGIC;
    sram[NIC_SRAM_DATA_CFG/4] = NIC_SRAM_DATA_CFG_ASF_ENABLE;
    assert(tg3_reset_chip() && tg3_asf);
    assert(sram[NIC_SRAM_FW_CMD_MBOX/4] == FWCMD_NICDRV_PAUSE_FW);
    setup(); grant = 0;
    assert(!tg3_reset_chip() && !reset_count && total_delay == 160000);
    assert(regs[NVRAM_SWARB/4] == SWARB_REQ_CLR1);
    setup(); acknowledge = 0;
    assert(tg3_reset_chip() && firmware_warnings == 1 && total_delay >= 1000000);
    setup();
    sram[NIC_SRAM_DATA_SIG/4] = NIC_SRAM_DATA_SIG_MAGIC;
    sram[NIC_SRAM_DATA_CFG/4] = NIC_SRAM_DATA_CFG_APE_ENABLE;
    assert(!tg3_reset_chip() && !reset_count);
    setup(); tg3_asic = 0x57780; tg3_cpmu = 1;
    regs[PCIE_PWR_MGMT_THRESH/4] = 0x55;
    regs[TG3_PCIE_EIDLE_DELAY/4] = 0xFF;
    regs[TG3_PCIE_LNKCTL/4] = 0x18;
    tg3_apply_57780_quirks();
    assert((regs[PCIE_PWR_MGMT_THRESH/4] & 0x0100FFFF) == 0x0100FF55);
    assert((regs[TG3_PCIE_EIDLE_DELAY/4] & 31) == 12);
    assert(!(regs[TG3_PCIE_LNKCTL/4] & 8) && (regs[TG3_PCIE_LNKCTL/4] & 0x80));
    assert(regs[TG3_RDMA_RSRVCTRL_REG/4] & TG3_RDMA_RSRVCTRL_FIFO_OFLW_FIX);
    assert(tg3_mdio_mode() == 0x8000);
    tg3_cpmu = 0; assert(tg3_mdio_mode() == MAC_MI_MODE_BASE);
    puts("PASS: Tg3 reset avoids inaccessible MMIO, restores PCI before MMIO, restores MAC, handles ASF/NVRAM/firmware timeouts and APE rejection");
}

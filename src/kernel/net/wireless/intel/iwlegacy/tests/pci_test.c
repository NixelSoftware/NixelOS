#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Kernel lock is not needed in this single-threaded PCI/MMIO fixture. */
#define KERNEL_MUTEX_H
#define MUTEX_GUARD(lock) ((void)0)
#include "../iwlegacy.c"

static pci_device_t devices[3];
static uint32_t registers[1024];
static uint16_t command, subsystem;
static unsigned count, bar_probes, config_writes;
static int bar_bad, enable_bad;
uint32_t pci_device_count(void) { return count; }
const pci_device_t* pci_device_at(uint32_t i) { return i < count ? devices + i : 0; }
uint16_t pci_read16(const pci_device_t* dev, uint16_t reg) {
    assert(dev == &devices[1]);
    if (reg == 4) return command;
    assert(reg == 0x2E); return subsystem;
}
void pci_write16(const pci_device_t* dev, uint16_t reg, uint16_t value) {
    assert(dev == &devices[1] && reg == 4); config_writes++; command = value;
}
int pci_probe_bar(const pci_device_t* dev, uint8_t index, pci_bar_t* bar) {
    assert(dev == &devices[1] && index == 0 && !(command & PCI_COMMAND_MASTER));
    bar_probes++;
    *bar = (pci_bar_t){(uint64_t)(unsigned long)registers, sizeof(registers), 0,0,0};
    if (bar_bad) bar->io = 1;
    return 1;
}
int pci_enable_device(const pci_device_t* dev, uint16_t decode) {
    assert(dev == &devices[1] && decode == PCI_COMMAND_MEMORY);
    if (enable_bad) return 0;
    command |= decode; return 1;
}
void driver_delay_us(uint32_t us) { (void)us; }
static void setup(void) {
    memset(devices,0,sizeof devices); memset(registers,0,sizeof registers);
    devices[0] = (pci_device_t){.vendor_id=0x8086,.device_id=0x100E,.class_code=2,.subclass=0};
    devices[1] = (pci_device_t){.bus=3,.slot=7,.function=1,.vendor_id=0x8086,.device_id=0x4222,
        .class_code=2,.subclass=0x80,.revision=2};
    devices[2] = (pci_device_t){.vendor_id=0x14E4,.device_id=0x4318,.class_code=2,.subclass=0x80};
    count = 3; command = 1; subsystem = 0x1005;
    bar_probes = config_writes = 0; bar_bad = enable_bad = 0;
}
int main(void) {
    iwlegacy_info_t info;
    setup(); count = 1;
    iwlegacy_probe(&info); assert(info.state == IL_ABSENT && !info.vendor && !bar_probes);
    setup(); command |= PCI_COMMAND_MASTER;
    iwlegacy_probe(&info); assert(info.state == IL_DEVICE_BUSY && !bar_probes && !config_writes);
    assert(info.vendor == 0x8086 && info.device == 0x4222 && info.bg_only);
    assert(info.pci_bus == 3 && info.pci_slot == 7 && info.pci_function == 1 && info.revision == 2);
    setup(); bar_bad = 1;
    iwlegacy_probe(&info); assert(info.state == IL_ACCESS_FAILED && command == 1);
    setup(); enable_bad = 1;
    iwlegacy_probe(&info); assert(info.state == IL_ACCESS_FAILED && command == 1);
    setup(); registers[CSR_GP_CNTRL/4] = CSR_GP_CNTRL_REG_FLAG_INIT_DONE;
    registers[CSR_INT_MASK/4] = 0x1234;
    iwlegacy_probe(&info); assert(info.state == IL_DEVICE_BUSY && command == 1);
    assert(registers[CSR_INT_MASK/4] == 0x1234); /* No writes to an active NIC. */
    setup(); registers[CSR_GP_CNTRL/4] = 0xFFFFFFFF;
    iwlegacy_probe(&info); assert(info.state == IL_ACCESS_FAILED && info.error == IL_ACCESS && command == 1);
    setup();
    iwlegacy_probe(&info); assert(info.state == IL_CLOCK_TIMEOUT && command == 1);
    assert(registers[CSR_INT_MASK/4] == 0 && (registers[CSR_RESET/4] & CSR_RESET_REG_FLAG_SW_RESET));
    puts("iwlegacy PCI: independent discovery, metadata, busy ownership and config cleanup passed");
    return 0;
}

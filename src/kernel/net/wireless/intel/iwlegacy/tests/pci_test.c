#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Kernel lock is not needed in this single-threaded PCI/MMIO fixture. */
#define KERNEL_MUTEX_H
#define MUTEX_GUARD(lock) ((void)(&(lock)))
typedef struct { unsigned depth; } mutex_t;
static mutex_t kernel_mutex;
#include "../iwlegacy.c"

static pci_device_t devices[3];
static uint32_t registers[1024];
static uint16_t command, subsystem;
static uint8_t retry_timeout;
static unsigned count, bar_probes, config_writes;
static int bar_bad, enable_bad, acknowledge_stop, map_bad;
static uint16_t link_control,parent_link_control,bridge_buses;
static int link_write_blocked;
int paging_map_mmio_page(uint32_t physical) {
    assert(physical == (uint32_t)(unsigned long)registers);
    assert(!(command & PCI_COMMAND_MASTER));
    return !map_bad;
}
uint8_t pci_find_capability(const pci_device_t* dev, uint8_t cap) {
    assert((dev == devices + 1 || dev == devices) && cap == PCI_CAP_PCIE); return 0x40;
}
uint32_t pci_device_count(void) { return count; }
const pci_device_t* pci_device_at(uint32_t i) { return i < count ? devices + i : 0; }
uint16_t pci_read16(const pci_device_t* dev, uint16_t reg) {
    if(dev==devices) {
        if(reg==0x18) return bridge_buses;
        assert(reg==0x50); return parent_link_control;
    }
    assert(dev == &devices[1]);
    if (reg == 4) return command;
    if (reg == 0x50) return link_control;
    assert(reg == 0x2E); return subsystem;
}
void pci_write8(const pci_device_t* dev, uint16_t reg, uint8_t value) {
    assert(dev == &devices[1] && reg == 0x41);
    assert(!(command & PCI_COMMAND_MASTER));
    retry_timeout = value;
}
void pci_write16(const pci_device_t* dev, uint16_t reg, uint16_t value) {
    if(reg==0x50) {
        assert(!(command&PCI_COMMAND_MASTER));
        if(dev==devices) parent_link_control=value;
        else { assert(dev==devices+1); if(!link_write_blocked) link_control=value; }
        return;
    }
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
void driver_delay_us(uint32_t us) {
    (void)us;
    if (acknowledge_stop && (registers[CSR_RESET/4] & CSR_RESET_REG_FLAG_STOP_MASTER))
        registers[CSR_RESET/4] |= CSR_RESET_REG_FLAG_MASTER_DISABLED;
}
static void setup(void) {
    memset(devices,0,sizeof devices); memset(registers,0,sizeof registers);
    devices[0] = (pci_device_t){.vendor_id=0x8086,.device_id=0x100E,.class_code=2,.subclass=0};
    devices[1] = (pci_device_t){.bus=3,.slot=7,.function=1,.vendor_id=0x8086,.device_id=0x4222,
        .class_code=2,.subclass=0x80,.revision=2};
    devices[2] = (pci_device_t){.vendor_id=0x14E4,.device_id=0x4318,.class_code=2,.subclass=0x80};
    parent_link_control=bridge_buses=0; link_write_blocked=0;
    count = 3; command = 1; subsystem = 0x1005; retry_timeout = 0x80;
    bar_probes = config_writes = 0; bar_bad = enable_bad = acknowledge_stop = map_bad = 0; link_control = 0;
}
int main(void) {
    iwlegacy_info_t info;
    setup(); map_bad = 1;
    iwlegacy_probe(&info); assert(info.error == IL_ACCESS && command == 1);
    setup(); count = 1;
    iwlegacy_probe(&info); assert(info.state == IL_ABSENT && !info.vendor && !bar_probes);
    setup(); command |= PCI_COMMAND_MASTER;
    iwlegacy_probe(&info); assert(info.state == IL_CLOCK_TIMEOUT && info.error == IL_TIMEOUT);
    assert(bar_probes == 1 && config_writes && !(command & PCI_COMMAND_MASTER));
    assert(info.vendor == 0x8086 && info.device == 0x4222 && info.bg_only);
    assert(info.pci_bus == 3 && info.pci_slot == 7 && info.pci_function == 1 && info.revision == 2);
    setup(); bar_bad = 1;
    iwlegacy_probe(&info); assert(info.state == IL_ACCESS_FAILED && command == 1);
    setup(); enable_bad = 1;
    iwlegacy_probe(&info); assert(info.state == IL_ACCESS_FAILED && command == 1);
    setup(); registers[CSR_GP_CNTRL/4] = CSR_GP_CNTRL_REG_FLAG_INIT_DONE;
    registers[CSR_INT_MASK/4] = 0x1234;
    iwlegacy_probe(&info); assert(info.state == IL_CLOCK_TIMEOUT && info.error == IL_TIMEOUT && command == 1);
    assert(registers[CSR_INT_MASK/4] == 0 && (registers[CSR_RESET/4] & CSR_RESET_REG_FLAG_SW_RESET));
    /* An actual native owner must still be left untouched. */
    setup(); iwlegacy_native_owned = 1;
    iwlegacy_probe(&info); assert(info.state == IL_DEVICE_BUSY && !bar_probes && !config_writes);
    iwlegacy_native_owned = 0;
    /* Inherited PCI DMA + INIT_DONE with acknowledged reset proceeds to EEPROM inspection. */
    setup(); command |= PCI_COMMAND_MASTER; registers[CSR_GP_CNTRL/4] = CSR_GP_CNTRL_REG_FLAG_INIT_DONE;
    registers[CSR_RESET/4] = CSR_RESET_REG_FLAG_MASTER_DISABLED;
    iwlegacy_probe(&info); assert(info.state == IL_CLOCK_TIMEOUT && command == 1);
    setup(); registers[CSR_GP_CNTRL/4] = 0xFFFFFFFF;
    iwlegacy_probe(&info); assert(info.state == IL_ACCESS_FAILED && info.error == IL_ACCESS && command == 1);
    setup();
    iwlegacy_probe(&info); assert(info.state == IL_CLOCK_TIMEOUT && command == 1);
    assert(registers[CSR_INT_MASK/4] == 0 && (registers[CSR_RESET/4] & CSR_RESET_REG_FLAG_SW_RESET));
    puts("iwlegacy PCI: independent discovery, metadata, busy ownership and config cleanup passed");
    return 0;
}

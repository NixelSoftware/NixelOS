/* SPDX-License-Identifier: GPL-2.0-only
 * Intel 3945 native PCI attachment. Register/EEPROM implementation adapted
 * from Intel's Linux iwlegacy driver; see README.md for scope and provenance.
 */
#include "iwlegacy.h"
#include "csr.h"
#include "../../../../mutex.h"
#include "../../../../drivers/delay.h"
static uint32_t mmio_read(void* base, uint32_t reg) {
    return *(volatile uint32_t*)((volatile uint8_t*)base + reg);
}
static void mmio_write(void* base, uint32_t reg, uint32_t value) {
    *(volatile uint32_t*)((volatile uint8_t*)base + reg) = value;
}
static void delay(void* ctx, uint32_t us) { (void)ctx; driver_delay_us(us); }
void iwlegacy_probe(iwlegacy_info_t* info) {
    MUTEX_GUARD(kernel_mutex);
    if (!info) return;
    *info = (iwlegacy_info_t){0};
    for (uint32_t index = 0; index < pci_device_count(); index++) {
        const pci_device_t* dev = pci_device_at(index);
        if (!dev || dev->class_code != 2 || dev->subclass != 0x80 ||
            !il3945_matches(dev->vendor_id, dev->device_id)) continue;
        info->state = IL_PCI_FOUND;
        info->vendor = dev->vendor_id; info->device = dev->device_id;
        info->pci_bus = dev->bus; info->pci_slot = dev->slot;
        info->pci_function = dev->function; info->revision = dev->revision;
        info->subsystem_device = pci_read16(dev, 0x2E);
        info->bg_only = il3945_bg_only(info->device, info->subsystem_device);
        uint16_t command = pci_read16(dev, 4);
        /* Do not resize BARs, reset or claim EEPROM from a live DMA owner. */
        if (command & PCI_COMMAND_MASTER) { info->state = IL_DEVICE_BUSY; return; }
        info->state = IL_ACCESS_FAILED;
        pci_bar_t bar;
        if (!pci_probe_bar(dev, 0, &bar) || bar.io || !bar.address ||
            bar.address > 0xFFFFF000ull || bar.size < 4096 ||
            !pci_enable_device(dev, PCI_COMMAND_MEMORY)) {
            pci_write16(dev, 4, command); return;
        }
        pci_write16(dev, 4, command | PCI_COMMAND_MEMORY | PCI_COMMAND_INTX_OFF);
        il_io_t io = {(void*)(uint32_t)bar.address, mmio_read, mmio_write, delay};
        uint32_t gp = mmio_read(io.context, CSR_GP_CNTRL);
        if (gp == 0xFFFFFFFFu) info->error = IL_ACCESS;
        else if (gp & CSR_GP_CNTRL_REG_FLAG_INIT_DONE) info->state = IL_DEVICE_BUSY;
        else {
            mmio_write(io.context, CSR_INT_MASK, 0);
            (void)il3945_inspect(&io, info->bg_only, info);
        }
        /* Inspection leaves the NIC stopped, with no DMA buffers or live RF. */
        pci_write16(dev, 4, command);
        return;
    }
}
const char* iwlegacy_description(iwlegacy_state_t state) {
    switch (state) {
    case IL_PCI_FOUND: return "Intel 3945 detected; initialization pending.";
    case IL_DEVICE_BUSY: return "Intel 3945 already active; inspection skipped.";
    case IL_ACCESS_FAILED: return "Intel 3945 register access failed.";
    case IL_CLOCK_TIMEOUT: return "Intel 3945 clock initialization timed out.";
    case IL_EEPROM_FAILED: return "Intel 3945 EEPROM read or validation failed.";
    case IL_EEPROM_READY: return "Intel 3945 EEPROM ready; packet transport not ported.";
    default: return "No Intel 3945 adapter found.";
    }
}

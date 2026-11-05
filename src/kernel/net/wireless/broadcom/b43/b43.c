/* SPDX-License-Identifier: GPL-2.0-or-later
 * Broadcom b43 native discovery. SSB IDs/register definitions from Linux
 * ssb_regs.h and b43_pci_bridge.c (Michael Buesch and Broadcom).
 * The Linux b43 reference delegates this work to SSB/BCMA.
 */
#include "b43.h"
#include "../../../../mutex.h"
#include "../../../../drivers/delay.h"

static int bus_type(uint16_t id) {
    switch (id) {
    case 0x4301: case 0x4306: case 0x4307: case 0x4311: case 0x4312:
    case 0x4315: case 0x4318: case 0x4319: case 0x4320: case 0x4321:
    case 0x4322: case 43222: case 0x4324: case 0x4325: case 0x4328:
    case 0x4329: case 0x432B: case 0x432C: case 0x4350: case 0x4351:
        return 1; /* SSB */
    case 0x4313: case 0x4331: case 0x4353: case 0x4357: case 0x4358:
    case 0x4359: case 0x4360: case 0x43A0: case 0x43B1:
        return 2; /* BCMA, requires EROM enumeration */
    default: return 0;
    }
}
static int select_core(const pci_device_t* dev, uint32_t address) {
    for (int retry = 0; retry < 10; retry++) {
        pci_write32(dev, 0x80, address);
        if (pci_read32(dev, 0x80) == address) return 1;
        driver_delay_us(10);
    }
    return 0;
}
void b43_probe(b43_info_t* info) {
    MUTEX_GUARD(kernel_mutex);
    if (!info) return;
    *info = (b43_info_t){0};
    for (uint32_t i = 0; i < pci_device_count(); i++) {
        const pci_device_t* dev = pci_device_at(i);
        if (!dev || dev->class_code != 2 || dev->subclass != 0x80) continue;
        int bus = dev->vendor_id == 0x14E4 ? bus_type(dev->device_id) :
                  dev->vendor_id == 0x14A4 && dev->device_id == 0x4318 ? 1 : 0;
        if (!bus) continue;
        info->vendor = dev->vendor_id; info->device = dev->device_id;
        info->state = B43_BUS_UNSUPPORTED;
        if (bus != 1) return;
        info->state = B43_PROBE_FAILED;
        pci_bar_t bar;
        /* Never disturb DMA from an existing owner. Size only an idle BAR. */
        uint16_t command = pci_read16(dev, 4);
        if ((command & PCI_COMMAND_MASTER) || !pci_probe_bar(dev, 0, &bar) ||
            bar.io || !bar.address || bar.address > 0xFFFFF000ull ||
            bar.size < 4096) return;
        /* Discovery does not wake a suspended card or reset its BARs. */
        uint8_t pm = pci_find_capability(dev, PCI_CAP_PM);
        if (pm && (pm > 0xF8 || (pci_read16(dev, pm + 4) & 3))) return;
        uint32_t window = pci_read32(dev, 0x80);
        pci_write16(dev, 4, command | PCI_COMMAND_MEMORY);
        if (!(pci_read16(dev, 4) & PCI_COMMAND_MEMORY)) {
            pci_write16(dev, 4, command); return;
        }
        volatile uint32_t* regs = (volatile uint32_t*)(uint32_t)bar.address;
        /* SSB enumeration space has at most 16 cores. Only read ID registers
         * on dormant cores. Never read PHY/MMIO registers while in reset. */
        for (uint8_t core = 0; core < 16; core++) {
            if (!select_core(dev, 0x18000000u + core * 4096u)) break;
            uint32_t id = regs[0xFFC / 4];
            if (id == 0xFFFFFFFFu || (id >> 16) != 0x4243) break;
            uint16_t code = (id & 0x8FF0) >> 4;
            if (code == 0x800) info->chip = regs[0] & 0xFFFF;
            if (code != 0x812) continue;
            info->core_index = core;
            info->core_revision = (id & 0xF) | ((id & 0x7000) >> 8);
            info->state = info->core_revision < 5 ? B43_LEGACY : B43_CORE_FOUND;
            break;
        }
        if (!select_core(dev, window)) info->state = B43_PROBE_FAILED;
        pci_write16(dev, 4, command);
        return;
    }
}
const char* b43_probe_description(b43_probe_state_t state) {
    switch (state) {
    case B43_BUS_UNSUPPORTED: return "BCMA bus support is not ported yet.";
    case B43_PROBE_FAILED: return "Cannot access the wireless SSB core.";
    case B43_LEGACY: return "This core requires b43legacy.";
    case B43_CORE_FOUND: return "b43 core detected; radio support incomplete.";
    default: return "No compatible Broadcom Wi-Fi adapter found.";
    }
}

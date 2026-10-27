/* Nixel PCI services, following the capability, power, enable and restore
 * ordering in documentation/linux-for-ref/pci.c. Native x86 implementation;
 * no Linux ABI emulation. Firmware owns bridge windows/BAR assignment. */
#include "pci.h"
#include "delay.h"
#ifndef PCI_HOST_TEST
#include "../task.h"
#include "../mutex.h"
static mutex_t inventory_mutex;
#define config_lock() task_irq_save()
#define config_unlock(flags) task_irq_restore(flags)
#else
/* Port-I/O model supplied by the host regression test. */
#include "pci_test_io.h"
#endif

#define CONFIG_ADDRESS 0xCF8
#define CONFIG_DATA 0xCFC
static pci_device_t devices[PCI_MAX_DEVICES];
static uint32_t device_count;
static uint8_t scanned, inventory_complete;

static int valid(uint8_t slot, uint8_t fn, uint16_t reg, uint8_t size) {
    return slot < 32 && fn < 8 && reg <= 256u - size && !(reg & (size - 1));
}
static void address(uint8_t bus, uint8_t slot, uint8_t fn, uint16_t reg) {
    outl(CONFIG_ADDRESS, 0x80000000u | (uint32_t)bus << 16 |
         (uint32_t)slot << 11 | (uint32_t)fn << 8 | (reg & 0xFC));
}
#define READ_CONFIG(bits, type) \
 type pci_config_read##bits(uint8_t bus, uint8_t slot, uint8_t fn, uint16_t reg) { \
    if (!valid(slot, fn, reg, sizeof(type))) return (type)~0u; \
    uint32_t flags = config_lock(); address(bus, slot, fn, reg); \
    type value = in##bits(CONFIG_DATA + (reg & 3)); config_unlock(flags); return value; \
 }
/* I/O helpers have byte/word/dword suffixes, rather than bit counts. */
#define in8 inb
#define in16 inw
#define in32 inl
#define out8 outb
#define out16 outw
#define out32 outl
READ_CONFIG(8, uint8_t)
READ_CONFIG(16, uint16_t)
READ_CONFIG(32, uint32_t)
#define WRITE_CONFIG(bits, type) \
 void pci_config_write##bits(uint8_t bus, uint8_t slot, uint8_t fn, uint16_t reg, type value) { \
    if (!valid(slot, fn, reg, sizeof(type))) return; \
    uint32_t flags = config_lock(); address(bus, slot, fn, reg); \
    out##bits(CONFIG_DATA + (reg & 3), value); config_unlock(flags); \
 }
WRITE_CONFIG(8, uint8_t)
WRITE_CONFIG(16, uint16_t)
WRITE_CONFIG(32, uint32_t)
#define DEVICE_ACCESS(bits, type) \
 type pci_read##bits(const pci_device_t* d, uint16_t r) { \
    return d ? pci_config_read##bits(d->bus, d->slot, d->function, r) : (type)~0u; \
 } \
 void pci_write##bits(const pci_device_t* d, uint16_t r, type v) { \
    if (d) pci_config_write##bits(d->bus, d->slot, d->function, r, v); \
 }
DEVICE_ACCESS(8, uint8_t)
DEVICE_ACCESS(16, uint16_t)
DEVICE_ACCESS(32, uint32_t)

int pci_init(void) {
    if (scanned) return inventory_complete;
#ifndef PCI_HOST_TEST
    MUTEX_GUARD(inventory_mutex);
#endif
    if (scanned) return inventory_complete;
    driver_delay_init();
    inventory_complete = 1;
    /* All firmware-configured buses, including devices behind PCI bridges.
     * Function zero's multifunction bit avoids 7 absent-function probes/slot. */
    for (uint32_t bus = 0; bus < 256; bus++)
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t id0 = pci_config_read32(bus, slot, 0, 0);
            if ((id0 & 0xFFFF) == 0xFFFF || !(id0 & 0xFFFF)) continue;
            uint8_t functions = (pci_config_read8(bus, slot, 0, 0x0E) & 0x80) ? 8 : 1;
            for (uint8_t fn = 0; fn < functions; fn++) {
                uint32_t id = fn ? pci_config_read32(bus, slot, fn, 0) : id0;
                if ((id & 0xFFFF) == 0xFFFF || !(id & 0xFFFF)) continue;
                if (device_count == PCI_MAX_DEVICES) { inventory_complete = 0; continue; }
                pci_device_t* d = &devices[device_count++];
                uint32_t cls = pci_config_read32(bus, slot, fn, 8);
                d->bus = bus; d->slot = slot; d->function = fn;
                d->vendor_id = id; d->device_id = id >> 16;
                d->revision = cls; d->prog_if = cls >> 8;
                d->subclass = cls >> 16; d->class_code = cls >> 24;
                d->header_type = pci_config_read8(bus, slot, fn, 0x0E) & 0x7F;
                d->irq_line = pci_config_read8(bus, slot, fn, 0x3C);
                d->irq_pin = pci_config_read8(bus, slot, fn, 0x3D);
            }
        }
    scanned = 1;
    return inventory_complete;
}
uint32_t pci_device_count(void) { pci_init(); return device_count; }
const pci_device_t* pci_device_at(uint32_t index) {
    pci_init(); return index < device_count ? &devices[index] : 0;
}
const pci_device_t* pci_find_device(uint16_t vendor, uint16_t device, uint32_t* cursor) {
    if (!cursor) return 0;
    pci_init();
    while (*cursor < device_count) {
        const pci_device_t* d = &devices[(*cursor)++];
        if ((vendor == PCI_ANY_ID || d->vendor_id == vendor) &&
            (device == PCI_ANY_ID || d->device_id == device)) return d;
    }
    return 0;
}
uint8_t pci_find_capability(const pci_device_t* d, uint8_t cap) {
    if (!d || !(pci_read16(d, 6) & 0x10)) return 0;
    uint16_t start = d->header_type == 2 ? 0x14 : 0x34;
    if (d->header_type > 2) return 0;
    uint8_t pos = pci_read8(d, start) & 0xFC;
    uint64_t visited = 0;
    for (uint8_t ttl = 48; pos >= 0x40 && ttl; ttl--) {
        uint64_t bit = 1ULL << (pos >> 2);
        if (visited & bit) break;
        visited |= bit;
        uint16_t value = pci_read16(d, pos);
        if ((value & 255) == 255) break;
        if ((value & 255) == cap) return pos;
        pos = (value >> 8) & 0xFC;
    }
    return 0;
}
static int bar_slot(const pci_device_t* d, uint8_t index) {
    uint8_t count = d && d->header_type == 0 ? 6 : d && d->header_type == 1 ? 2 : 0;
    if (index >= count) return 0;
    for (uint8_t i = 0; i < index; i++) {
        uint32_t value = pci_read32(d, 0x10 + i * 4);
        if (!(value & 1) && (value & 6) == 4 && ++i == index) return 0;
    }
    return 1;
}
int pci_get_bar(const pci_device_t* d, uint8_t index, pci_bar_t* bar) {
    if (!bar || !bar_slot(d, index)) return 0;
    uint16_t reg = 0x10 + index * 4;
    uint32_t low = pci_read32(d, reg);
    if (low == 0xFFFFFFFF || (!(low & 1) && (low & 6) == 6)) return 0;
    *bar = (pci_bar_t){0};
    bar->io = low & 1; bar->prefetchable = !bar->io && (low & 8);
    bar->width64 = !bar->io && (low & 6) == 4;
    if (bar->width64 && index == (d->header_type == 0 ? 5 : 1)) return 0;
    bar->address = low & (bar->io ? ~3u : ~15u);
    if (bar->width64) bar->address |= (uint64_t)pci_read32(d, reg + 4) << 32;
    return 1;
}
int pci_probe_bar(const pci_device_t* d, uint8_t index, pci_bar_t* bar) {
    if (!pci_get_bar(d, index, bar)) return 0;
    uint32_t flags = config_lock();
    uint16_t command = pci_read16(d, 4), reg = 0x10 + index * 4;
    uint32_t low = pci_read32(d, reg), high = bar->width64 ? pci_read32(d, reg + 4) : 0;
    pci_write16(d, 4, command & ~7u);
    pci_write32(d, reg, ~0u);
    if (bar->width64) pci_write32(d, reg + 4, ~0u);
    uint32_t mask_low = pci_read32(d, reg), mask_high = bar->width64 ? pci_read32(d, reg + 4) : 0;
    if (bar->width64) pci_write32(d, reg + 4, high);
    pci_write32(d, reg, low);
    pci_write16(d, 4, command);
    config_unlock(flags);
    uint64_t mask = mask_low & (bar->io ? ~3u : ~15u);
    if (bar->width64) mask |= (uint64_t)mask_high << 32;
    else mask |= 0xFFFFFFFF00000000ULL;
    if (!(mask_low & (bar->io ? ~3u : ~15u)) && !mask_high) return 0;
    /* Legacy I/O BARs can implement only 16 address bits; below-1-MiB
     * memory BARs implement 20. Unimplemented high bits are not size bits. */
    if (bar->io && !(mask_low & 0xFFFF0000u)) mask |= 0xFFFF0000u;
    if (!bar->io && (low & 6) == 2) mask |= 0xFFF00000u;
    bar->size = ~mask + 1;
    return bar->size && !(bar->size & (bar->size - 1));
}
int pci_power_on(const pci_device_t* d) {
    if (!d || pci_read16(d, 0) != d->vendor_id) return 0;
    uint8_t pm = pci_find_capability(d, PCI_CAP_PM);
    if (!pm) return 1;
    if (pm > 0xF8) return 0;
    uint16_t control = pci_read16(d, pm + 4), state = control & 3;
    if (state) {
        /* D3hot->D0 may reset BARs unless NO_SOFT_RESET is advertised. */
        uint32_t bars[6], rom = 0;
        uint8_t restore_bars = state == 3 && !(control & 8) && d->header_type == 0;
        if (restore_bars) {
            for (uint8_t i = 0; i < 6; i++) bars[i] = pci_read32(d, 0x10 + i * 4);
            rom = pci_read32(d, 0x30);
        }
        pci_write16(d, pm + 4, control & ~0x8003u);
        /* Linux PCI D3hot->D0 recovery interval; no scheduler dependency. */
        driver_delay_us(state == 3 ? 10000 : 200);
        if (restore_bars) {
            uint16_t command = pci_read16(d, 4) & ~PCI_COMMAND_MASTER;
            pci_write16(d, 4, command & ~3u);
            for (int i = 5; i >= 0; i--) pci_write32(d, 0x10 + i * 4, bars[i]);
            pci_write32(d, 0x30, rom);
            pci_write16(d, 4, command);
        }
    }
    return !(pci_read16(d, pm + 4) & 3);
}
int pci_enable_device(const pci_device_t* d, uint16_t decode) {
    if (!pci_power_on(d)) return 0;
    uint32_t flags = config_lock();
    uint16_t cmd = pci_read16(d, 4) | (decode & 3);
    pci_write16(d, 4, cmd);
    int ok = (pci_read16(d, 4) & decode & 3) == (decode & 3);
    config_unlock(flags);
    return ok;
}
void pci_set_master(const pci_device_t* d, int enabled) {
    if (!d) return;
    uint32_t flags = config_lock();
    uint16_t cmd = pci_read16(d, 4);
    pci_write16(d, 4, enabled ? cmd | PCI_COMMAND_MASTER : cmd & ~PCI_COMMAND_MASTER);
    config_unlock(flags);
}
void pci_disable_device(const pci_device_t* d) {
    if (!d) return;
    uint32_t flags = config_lock();
    pci_write16(d, 4, pci_read16(d, 4) & ~7u);
    config_unlock(flags);
}
void pci_prepare_polling(const pci_device_t* d) {
    if (!d) return;
    uint8_t cap = pci_find_capability(d, PCI_CAP_MSI);
    if (cap && cap <= 0xFC) pci_write16(d, cap + 2, pci_read16(d, cap + 2) & ~1u);
    cap = pci_find_capability(d, PCI_CAP_MSIX);
    if (cap && cap <= 0xFC) pci_write16(d, cap + 2, (pci_read16(d, cap + 2) | 0x4000u) & ~0x8000u);
    uint32_t flags = config_lock();
    pci_write16(d, 4, pci_read16(d, 4) | PCI_COMMAND_INTX_OFF);
    config_unlock(flags);
}
int pci_save_state(const pci_device_t* d, pci_saved_state_t* s) {
    if (!d || !s || d->header_type != 0 || pci_read16(d, 0) != d->vendor_id) return 0;
    *s = (pci_saved_state_t){0};
    s->command = pci_read16(d, 4);
    for (uint8_t i = 0; i < 6; i++) s->bars[i] = pci_read32(d, 0x10 + i * 4);
    s->rom = pci_read32(d, 0x30);
    s->cache_line = pci_read8(d, 0x0C); s->latency = pci_read8(d, 0x0D);
    s->irq_line = pci_read8(d, 0x3C);
    s->pcie_cap = pci_find_capability(d, PCI_CAP_PCIE);
    if (s->pcie_cap && s->pcie_cap <= 0xEC) {
        s->pcie_control = pci_read16(d, s->pcie_cap + 8);
        s->pcie_link_control = pci_read16(d, s->pcie_cap + 16);
    } else s->pcie_cap = 0;
    s->valid = 1;
    return 1;
}
int pci_restore_state(const pci_device_t* d, const pci_saved_state_t* s) {
    if (!d || !s || !s->valid || d->header_type != 0 || pci_read16(d, 0) != d->vendor_id) return 0;
    uint32_t flags = config_lock();
    pci_write16(d, 4, pci_read16(d, 4) & ~7u);
    for (int i = 5; i >= 0; i--) pci_write32(d, 0x10 + i * 4, s->bars[i]);
    pci_write32(d, 0x30, s->rom);
    pci_write8(d, 0x0C, s->cache_line); pci_write8(d, 0x0D, s->latency);
    pci_write8(d, 0x3C, s->irq_line);
    if (s->pcie_cap) {
        pci_write16(d, s->pcie_cap + 8, s->pcie_control);
        pci_write16(d, s->pcie_cap + 16, s->pcie_link_control);
    }
    /* Never restore PCI/PCIe status by writing a saved status dword. */
    pci_write16(d, 4, s->command);
    int ok = pci_read16(d, 4) == s->command;
    config_unlock(flags);
    return ok;
}

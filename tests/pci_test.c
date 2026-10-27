/* cc -O2 -Wall -Wextra -Itests tests/pci_test.c -o /tmp/pci_test && /tmp/pci_test */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define PCI_HOST_TEST
#include "../src/kernel/drivers/pci.c"

typedef struct {
    unsigned bus, slot, fn;
    uint8_t cfg[256];
    uint32_t masks[6];
} model_t;
static model_t model[3];
static uint32_t selected, depth, io_count, delay_total, delay_init;
static unsigned reset_bars_on_resume;
static unsigned write_regs[256], write_sizes[256], writes;
static model_t* selected_device(void) {
    assert(depth && (selected & 0x80000000u));
    for (unsigned i = 0; i < 3; i++)
        if (model[i].bus == ((selected >> 16) & 255) &&
            model[i].slot == ((selected >> 11) & 31) &&
            model[i].fn == ((selected >> 8) & 7)) return &model[i];
    return 0;
}
uint32_t pci_test_lock(void) { return depth++; }
void pci_test_unlock(uint32_t flags) { assert(depth == flags + 1); depth = flags; }
static uint32_t read_io(uint16_t port, unsigned size) {
    io_count++;
    model_t* d = selected_device();
    if (!d) return ~0u;
    unsigned reg = (selected & 0xFC) + (port - 0xCFC);
    uint32_t value = 0;
    for (unsigned i = 0; i < size; i++) value |= (uint32_t)d->cfg[reg+i] << (i*8);
    if (size == 4 && reg >= 0x10 && reg < 0x28 && value == ~0u) return d->masks[(reg-0x10)/4];
    return value;
}
uint8_t pci_test_inb(uint16_t port) { return read_io(port, 1); }
uint16_t pci_test_inw(uint16_t port) { return read_io(port, 2); }
uint32_t pci_test_inl(uint16_t port) { return read_io(port, 4); }
static void write_io(uint16_t port, uint32_t value, unsigned size) {
    io_count++;
    model_t* d = selected_device();
    if (!d) return;
    unsigned reg = (selected & 0xFC) + (port - 0xCFC);
    if (writes < 256) { write_regs[writes] = reg; write_sizes[writes++] = size; }
    for (unsigned i = 0; i < size; i++) {
        unsigned offset = reg + i;
        uint8_t byte = value >> (i*8);
        if (offset == 6 || offset == 7) d->cfg[offset] &= ~byte;
        else if (offset == 0x45) d->cfg[offset] = (byte & 0x7F) | (d->cfg[offset] & ~byte & 0x80);
        else d->cfg[offset] = byte;
    }
}
void pci_test_outb(uint16_t port, uint8_t value) { write_io(port, value, 1); }
void pci_test_outw(uint16_t port, uint16_t value) { write_io(port, value, 2); }
void pci_test_outl(uint16_t port, uint32_t value) {
    if (port == 0xCF8) { assert(depth); selected = value; io_count++; }
    else write_io(port, value, 4);
}
void driver_delay_init(void) { delay_init++; }
void driver_delay_us(uint32_t us) {
    delay_total += us;
    if (reset_bars_on_resume && us == 10000) {
        memset(model[1].cfg + 0x10, 0, 24);
        reset_bars_on_resume = 0;
    }
}
static void put32(unsigned dev, unsigned reg, uint32_t value) {
    for (unsigned i = 0; i < 4; i++) model[dev].cfg[reg+i] = value >> (i*8);
}
int main(void) {
    model[0].slot = model[1].slot = 1; model[1].fn = 3;
    model[2].bus = 2; model[2].slot = 7;
    for (unsigned i = 0; i < 3; i++) {
        put32(i, 0, (0x1677u << 16) | 0x14E4);
        put32(i, 8, 0x02000001);
        model[i].cfg[0x0E] = i == 0 ? 0x80 : 0;
    }
    put32(1, 4, 0xA5100007); /* W1C status must survive command writes. */
    model[1].cfg[0x34] = 0x40;
    put32(1, 0x40, 0x5001); put32(1, 0x44, 0x8003);
    put32(1, 0x50, 0x6010); put32(1, 0x58, 0xDEAD0810);
    put32(1, 0x60, 0x00017005); put32(1, 0x70, 0x80000011);
    put32(1, 0x10, 0xF0000000); model[1].masks[0] = 0xFFFF0000;
    put32(1, 0x18, 0xF1000004); model[1].masks[2] = 0xFFFE0004;
    put32(1, 0x1C, 0); model[1].masks[3] = ~0u;
    assert(pci_init() && pci_device_count() == 3 && delay_init == 1);
    unsigned scanned_io = io_count;
    assert(pci_init() && io_count == scanned_io); /* Inventory is cached. */
    assert(pci_device_at(2)->bus == 2);
    uint32_t cursor = 0;
    assert(pci_find_device(0x14E4, 0x1677, &cursor)->function == 0);
    const pci_device_t* d = pci_find_device(0x14E4, 0x1677, &cursor);
    assert(d && d->function == 3);
    assert(pci_find_capability(d, PCI_CAP_PCIE) == 0x50);
    assert(pci_find_capability(d, PCI_CAP_PM) == 0x40);
    reset_bars_on_resume = 1;
    assert(pci_enable_device(d, PCI_COMMAND_MEMORY) && delay_total == 10000);
    assert(pci_read32(d, 0x10) == 0xF0000000);
    assert(pci_read16(d, 6) == 0xA510 && (pci_read16(d, 0x44) & 0x8000));
    pci_set_master(d, 0); assert(!(pci_read16(d, 4) & 4));
    pci_prepare_polling(d);
    assert(!(pci_read16(d, 0x62) & 1));
    assert((pci_read16(d, 0x72) & 0xC000) == 0x4000);
    assert(pci_read16(d, 6) == 0xA510);
    pci_bar_t bar;
    assert(pci_probe_bar(d, 0, &bar) && bar.address == 0xF0000000 && bar.size == 0x10000);
    assert(pci_probe_bar(d, 2, &bar) && bar.width64 && bar.size == 0x20000);
    assert(pci_read32(d, 0x18) == 0xF1000004 && pci_read32(d, 0x1C) == 0);
    assert(!pci_get_bar(d, 3, &bar)); /* Upper half is not a separate BAR. */
    assert(!pci_probe_bar(d, 1, &bar));
    put32(1, 0x20, 0xC001); model[1].masks[4] = 0xFF01;
    assert(pci_probe_bar(d, 4, &bar) && bar.io && bar.address == 0xC000 && bar.size == 256);
    put32(1, 0x24, 0x80002); model[1].masks[5] = 0xFF002;
    assert(pci_probe_bar(d, 5, &bar) && !bar.io && bar.size == 4096);
    pci_saved_state_t state;
    assert(pci_save_state(d, &state));
    put32(1, 0x10, 0); put32(1, 0x18, 0); put32(1, 4, 0xA5100000);
    writes = 0;
    assert(pci_restore_state(d, &state));
    assert(pci_read32(d, 0x10) == 0xF0000000 && pci_read16(d, 6) == 0xA510);
    assert(write_regs[writes-1] == 4 && write_sizes[writes-1] == 2);
    for (unsigned i = 0; i < writes; i++) assert(write_regs[i] != 6);
    model[1].cfg[0x71] = 0x40; /* Capability cycle must terminate. */
    assert(!pci_find_capability(d, 0xFE));
    scanned_io = io_count;
    assert(pci_config_read32(0, 32, 0, 0) == ~0u);
    assert(pci_config_read16(0, 1, 3, 255) == 0xFFFF);
    pci_config_write32(0, 1, 3, 2, ~0u);
    assert(io_count == scanned_io && !depth);
    puts("PASS: PCI enumeration/cache, config serialization/widths, PM/W1C, capabilities/cycles, BAR sizing/restore, reset state order");
}

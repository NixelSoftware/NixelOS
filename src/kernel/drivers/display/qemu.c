#include "video_drivers.h"
#include "../pci.h"

/* QEMU Standard VGA, PCI 1234:1111, Bochs DISPI interface.
 * https://www.qemu.org/docs/master/specs/standard-vga.html */
static void dispi_write(uint16_t index, uint16_t value) {
    outw(0x1CE, index);
    outw(0x1CF, value);
}
static uint16_t dispi_read(uint16_t index) {
    outw(0x1CE, index);
    return inw(0x1CF);
}
int qemu_video_init(video_framebuffer_t* fb) {
    pci_init();
    uint32_t cursor = 0;
    const pci_device_t* dev;
    while ((dev = pci_find_device(0x1234, 0x1111, &cursor))) {
        /* Legacy DISPI ports belong to the primary VGA device. */
        if (dev->class_code != 3 || dev->subclass != 0) continue;
        pci_bar_t bar;
        uint32_t bytes = SCREEN_WIDTH * SCREEN_HEIGHT * 4u;
        if (!pci_probe_bar(dev, 0, &bar) || bar.io || !bar.address ||
            bar.address > 0xFFFFFFFFu - bytes || bar.size < bytes) continue;
        if (!pci_enable_device(dev, PCI_COMMAND_IO | PCI_COMMAND_MEMORY)) continue;
        uint16_t id = dispi_read(0);
        if (id < 0xB0C2 || id > 0xB0C5) continue;
        /* Validate all resources before changing the firmware's fallback mode. */
        dispi_write(4, 0);
        dispi_write(1, SCREEN_WIDTH);
        dispi_write(2, SCREEN_HEIGHT);
        dispi_write(3, 32);
        dispi_write(6, SCREEN_WIDTH);
        dispi_write(8, 0);
        dispi_write(9, 0);
        dispi_write(4, 0x41); /* ENABLED | LFB_ENABLED */
        *fb = (video_framebuffer_t){(uint32_t)bar.address, 32,
                                    SCREEN_WIDTH * 4u, "QEMU Standard VGA"};
        return 1;
    }
    return 0;
}

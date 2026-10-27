#ifndef NIXEL_PCI_H
#define NIXEL_PCI_H
#include "../types.h"

/* Native, firmware-assigned PCI segment 0. Config mechanism #1: 256 bytes. */
#define PCI_COMMAND_IO       0x0001u
#define PCI_COMMAND_MEMORY   0x0002u
#define PCI_COMMAND_MASTER   0x0004u
#define PCI_COMMAND_INTX_OFF 0x0400u
#define PCI_CAP_PM           0x01
#define PCI_CAP_MSI          0x05
#define PCI_CAP_PCIE         0x10
#define PCI_CAP_MSIX         0x11
#define PCI_ANY_ID           0xFFFFu
#define PCI_MAX_DEVICES      128u

typedef struct {
    uint8_t bus, slot, function, header_type;
    uint16_t vendor_id, device_id;
    uint8_t class_code, subclass, prog_if, revision;
    uint8_t irq_line, irq_pin;
} pci_device_t;
typedef struct {
    uint64_t address, size;
    uint8_t io, prefetchable, width64;
} pci_bar_t;
typedef struct {
    uint32_t bars[6], rom;
    uint16_t command, pcie_control, pcie_link_control;
    uint8_t cache_line, latency, irq_line, pcie_cap, valid;
} pci_saved_state_t;

/* Config address/data pairs are serialized against interrupts/preemption.
 * Narrow writes never echo write-one-to-clear status bits. Invalid reads
 * return all ones; invalid writes do nothing. */
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t fn, uint16_t reg);
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t fn, uint16_t reg);
uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t fn, uint16_t reg);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t fn, uint16_t reg, uint32_t value);
void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t fn, uint16_t reg, uint16_t value);
void pci_config_write8(uint8_t bus, uint8_t slot, uint8_t fn, uint16_t reg, uint8_t value);
uint32_t pci_read32(const pci_device_t* dev, uint16_t reg);
uint16_t pci_read16(const pci_device_t* dev, uint16_t reg);
uint8_t pci_read8(const pci_device_t* dev, uint16_t reg);
void pci_write32(const pci_device_t* dev, uint16_t reg, uint32_t value);
void pci_write16(const pci_device_t* dev, uint16_t reg, uint16_t value);
void pci_write8(const pci_device_t* dev, uint16_t reg, uint8_t value);

/* One cached scan of firmware-configured buses, in BDF order. No heap or
 * Linux device-model/workqueue dependencies. Returns 0 on inventory overflow. */
int pci_init(void);
uint32_t pci_device_count(void);
const pci_device_t* pci_device_at(uint32_t index);
const pci_device_t* pci_find_device(uint16_t vendor, uint16_t device, uint32_t* cursor);
uint8_t pci_find_capability(const pci_device_t* dev, uint8_t capability);
int pci_get_bar(const pci_device_t* dev, uint8_t index, pci_bar_t* bar);
/* Sizing temporarily disables decode and bus mastering. Use only before a
 * driver starts DMA; not an operation for a running device. */
int pci_probe_bar(const pci_device_t* dev, uint8_t index, pci_bar_t* bar);
int pci_power_on(const pci_device_t* dev);
int pci_enable_device(const pci_device_t* dev, uint16_t decode);
void pci_set_master(const pci_device_t* dev, int enabled);
void pci_disable_device(const pci_device_t* dev);
void pci_prepare_polling(const pci_device_t* dev);
int pci_save_state(const pci_device_t* dev, pci_saved_state_t* state);
int pci_restore_state(const pci_device_t* dev, const pci_saved_state_t* state);
#endif

#ifndef NIXEL_PCI_TEST_IO_H
#define NIXEL_PCI_TEST_IO_H
uint32_t pci_test_lock(void);
void pci_test_unlock(uint32_t flags);
uint8_t pci_test_inb(uint16_t port);
uint16_t pci_test_inw(uint16_t port);
uint32_t pci_test_inl(uint16_t port);
void pci_test_outb(uint16_t port, uint8_t value);
void pci_test_outw(uint16_t port, uint16_t value);
void pci_test_outl(uint16_t port, uint32_t value);
#define config_lock() pci_test_lock()
#define config_unlock(flags) pci_test_unlock(flags)
#define inb pci_test_inb
#define inw pci_test_inw
#define inl pci_test_inl
#define outb pci_test_outb
#define outw pci_test_outw
#define outl pci_test_outl
#endif

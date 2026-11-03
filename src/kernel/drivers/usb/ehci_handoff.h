#ifndef KERNEL_USB_EHCI_HANDOFF_H
#define KERNEL_USB_EHCI_HANDOFF_H
#include "../pci.h"
int ehci_claim_firmware(const pci_device_t* device,unsigned offset);
/* Route ports to supported UHCI companions; this is not an EHCI data driver. */
unsigned ehci_handoff_to_uhci(void);
#endif

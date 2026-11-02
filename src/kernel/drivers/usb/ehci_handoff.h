#ifndef KERNEL_USB_EHCI_HANDOFF_H
#define KERNEL_USB_EHCI_HANDOFF_H
/* Route ports to supported UHCI companions; this is not an EHCI data driver. */
unsigned ehci_handoff_to_uhci(void);
#endif

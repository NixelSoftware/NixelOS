/* SPDX-License-Identifier: GPL-2.0-only
 * Match iwlegacy's probe-time pci_disable_link_state(L0S | L1 | CLKPM).
 * Firmware allow-sleep and the NIC's internal L1 flag do not configure the
 * upstream PCIe link. Apply before bus mastering, and verify both partners.
 */
#include "internal.h"
#define LINK_POWER_BITS 0x0103u
static int disable(const pci_device_t* dev,uint8_t cap,uint16_t* before,uint16_t* after) {
    *before=pci_read16(dev,cap+0x10);
    if(*before==0xffffu) return IL_ACCESS;
    if(*before&LINK_POWER_BITS) pci_write16(dev,cap+0x10,*before&~LINK_POWER_BITS);
    *after=pci_read16(dev,cap+0x10);
    return *after==0xffffu || (*after&LINK_POWER_BITS) ? IL_ACCESS : IL_OK;
}
int il3945_disable_link_power(const pci_device_t* dev,il3945_link_power_t* out) {
    if(!dev || !out) return IL_INVALID;
    *out=(il3945_link_power_t){0};
    uint8_t cap=pci_find_capability(dev,PCI_CAP_PCIE);
    if(!cap || cap>0xec) return IL_ACCESS;
    /* A bridge whose secondary bus equals our bus is the immediate parent.
     * Do not touch unrelated ports, sibling devices or bus numbering. */
    for(unsigned i=0;i<pci_device_count();i++) {
        const pci_device_t* parent=pci_device_at(i);
        if(!parent || parent==dev || parent->class_code!=6 || parent->subclass!=4) continue;
        uint16_t buses=pci_read16(parent,0x18);
        if(buses==0xffffu || (buses>>8)!=dev->bus) continue;
        uint8_t pc=pci_find_capability(parent,PCI_CAP_PCIE);
        if(!pc) break; /* Conventional bridge, no PCIe Link Control. */
        if(pc>0xec) return IL_ACCESS;
        out->parent_present=1;
        int r=disable(parent,pc,&out->parent_before,&out->parent_after);
        if(r) return r;
        break;
    }
    out->valid=1;
    return disable(dev,cap,&out->endpoint_before,&out->endpoint_after);
}

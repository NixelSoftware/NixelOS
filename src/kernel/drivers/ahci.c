#include "ahci.h"
#include "../arch_i386.h"
#include "../paging.h"
#include "serial.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static inline uint32_t pci_cfg_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((1U << 31) | ((uint32_t)bus << 16) |
                                  ((uint32_t)slot << 11) | ((uint32_t)func << 8) |
                                  (offset & 0xFC));
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static inline uint16_t pci_cfg_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((1U << 31) | ((uint32_t)bus << 16) |
                                  ((uint32_t)slot << 11) | ((uint32_t)func << 8) |
                                  (offset & 0xFC));
    outl(PCI_CONFIG_ADDRESS, address);
    return (uint16_t)((inl(PCI_CONFIG_DATA) >> ((offset & 2) * 8)) & 0xFFFF);
}

static inline void pci_cfg_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    uint32_t address = (uint32_t)((1U << 31) | ((uint32_t)bus << 16) |
                                  ((uint32_t)slot << 11) | ((uint32_t)func << 8) |
                                  (offset & 0xFC));
    outl(PCI_CONFIG_ADDRESS, address);
    outw(PCI_CONFIG_DATA + (offset & 2), value);
}

static hba_mem_t* abar = 0;
static uint32_t ports_active_mask = 0;

static void port_stop_cmd(hba_port_t* port) {
    port->cmd &= ~HBA_PxCMD_ST;
    port->cmd &= ~HBA_PxCMD_FRE;

    while (1) {
        if (port->cmd & HBA_PxCMD_FR) continue;
        if (port->cmd & HBA_PxCMD_CR) continue;
        break;
    }
}

static void port_start_cmd(hba_port_t* port) {
    while (port->cmd & HBA_PxCMD_CR);
    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
}

static void ahci_port_rebase(hba_port_t* port, int port_no) {
    port_stop_cmd(port);

    /* Allocate physically contiguous frame for Command List and Received FIS */
    uint32_t clb_phys = paging_frame_alloc();
    uint32_t fb_phys  = clb_phys + 1024;

    port->clb  = clb_phys;
    port->clbu = 0;
    port->fb   = fb_phys;
    port->fbu  = 0;

    /* Initialize 32 command headers */
    hba_cmd_header_t* cmd_header = (hba_cmd_header_t*)clb_phys;
    for (int i = 0; i < 32; i++) {
        cmd_header[i].prdtl = 8;
        uint32_t tbl_phys = paging_frame_alloc();
        cmd_header[i].ctba  = tbl_phys;
        cmd_header[i].ctbau = 0;
    }

    port_start_cmd(port);
    debug_print("[AHCI] Port rebased: ");
    debug_hex(port_no);
    debug_print("\n");
}

static int ahci_find_cmdslot(hba_port_t* port) {
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; i++) {
        if ((slots & 1) == 0) return i;
        slots >>= 1;
    }
    return -1;
}

static int ahci_access_sectors(uint8_t port_no, uint64_t lba, uint32_t count, void* buf, int is_write) {
    if (!abar || !(ports_active_mask & (1 << port_no))) return -1;

    hba_port_t* port = &abar->ports[port_no];
    port->is = (uint32_t)-1;

    int slot = ahci_find_cmdslot(port);
    if (slot == -1) return -1;

    hba_cmd_header_t* cmd_header = (hba_cmd_header_t*)(port->clb);
    cmd_header += slot;
    cmd_header->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    cmd_header->w = is_write ? 1 : 0;
    cmd_header->prdtl = 1;

    hba_cmd_table_t* cmd_tbl = (hba_cmd_table_t*)(cmd_header->ctba);
    uint8_t* p = (uint8_t*)cmd_tbl;
    for (uint32_t i = 0; i < sizeof(hba_cmd_table_t) + sizeof(hba_prdt_entry_t); i++) p[i] = 0;

    cmd_tbl->prdt_entry[0].dba = (uint32_t)buf;
    cmd_tbl->prdt_entry[0].dbau = 0;
    cmd_tbl->prdt_entry[0].dbc = (count << 9) - 1; /* 512 bytes per sector */
    cmd_tbl->prdt_entry[0].i = 1;

    fis_reg_h2d_t* cmdfis = (fis_reg_h2d_t*)(&cmd_tbl->cfis);
    cmdfis->fis_type = 0x27; /* Register FIS - Host to Device */
    cmdfis->c = 1;
    cmdfis->command = is_write ? ATA_CMD_WRITE_DMA_EX : ATA_CMD_READ_DMA_EX;

    cmdfis->lba0 = (uint8_t)lba;
    cmdfis->lba1 = (uint8_t)(lba >> 8);
    cmdfis->lba2 = (uint8_t)(lba >> 16);
    cmdfis->device = 1 << 6; /* LBA mode */

    cmdfis->lba3 = (uint8_t)(lba >> 24);
    cmdfis->lba4 = (uint8_t)(lba >> 32);
    cmdfis->lba5 = (uint8_t)(lba >> 40);

    cmdfis->countl = (uint8_t)(count & 0xFF);
    cmdfis->counth = (uint8_t)((count >> 8) & 0xFF);

    /* Spin until device is not busy */
    int spin = 0;
    while ((port->tfd & (0x80 | 0x08)) && spin < 1000000) spin++;
    if (spin == 1000000) return -2;

    port->ci = 1 << slot;

    while (1) {
        if ((port->ci & (1 << slot)) == 0) break;
        if (port->is & (1 << 30)) return -3; /* Task File Error */
    }

    if (port->is & (1 << 30)) return -3;
    return 0;
}

int ahci_read(uint8_t port_no, uint64_t lba, uint32_t count, void* buf) {
    return ahci_access_sectors(port_no, lba, count, buf, 0);
}

int ahci_write(uint8_t port_no, uint64_t lba, uint32_t count, const void* buf) {
    return ahci_access_sectors(port_no, lba, count, (void*)buf, 1);
}

static void probe_ports(hba_mem_t* mem) {
    uint32_t pi = mem->pi;
    for (int i = 0; i < 32; i++) {
        if (pi & 1) {
            hba_port_t* port = &mem->ports[i];
            uint32_t ssts = port->ssts;
            uint8_t ipm = (ssts >> 8) & 0x0F;
            uint8_t det = ssts & 0x0F;

            if (det == HBA_PORT_DET_PRESENT && ipm == HBA_PORT_IPM_ACTIVE) {
                if (port->sig == AHCI_DEV_SATA) {
                    debug_print("[AHCI] SATA drive detected on port ");
                    debug_hex(i);
                    debug_print("\n");
                    ahci_port_rebase(port, i);
                    ports_active_mask |= (1 << i);
                }
            }
        }
        pi >>= 1;
    }
}

void ahci_init(void) {
    uint8_t found = 0;
    uint8_t ahci_bus = 0, ahci_slot = 0, ahci_func = 0;

    /* Scan PCI bus for Class 0x01 (Storage), Subclass 0x06 (SATA), Prog-IF 0x01 (AHCI) */
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint16_t vendor = pci_cfg_read16(bus, slot, func, 0x00);
                if (vendor == 0xFFFF) continue;

                uint32_t class_reg = pci_cfg_read32(bus, slot, func, 0x08);
                uint8_t class_code = (class_reg >> 24) & 0xFF;
                uint8_t subclass   = (class_reg >> 16) & 0xFF;
                uint8_t prog_if    = (class_reg >> 8) & 0xFF;

                if (class_code == AHCI_PCI_CLASS_STORAGE &&
                    subclass == AHCI_PCI_SUBCLASS_SATA &&
                    prog_if == AHCI_PCI_PROG_IF_AHCI) {
                    ahci_bus = (uint8_t)bus;
                    ahci_slot = slot;
                    ahci_func = func;
                    found = 1;
                    break;
                }

                if (func == 0) {
                    uint8_t header_type = (pci_cfg_read32(bus, slot, func, 0x0C) >> 16) & 0xFF;
                    if (!(header_type & 0x80)) break;
                }
            }
            if (found) break;
        }
        if (found) break;
    }

    if (!found) {
        debug_print("[AHCI] No AHCI controller found on PCI bus.\n");
        return;
    }

    /* Enable Bus Mastering (bit 2) and Memory Space (bit 1) in PCI Command register */
    uint16_t cmd = pci_cfg_read16(ahci_bus, ahci_slot, ahci_func, 0x04);
    pci_cfg_write16(ahci_bus, ahci_slot, ahci_func, 0x04, cmd | 0x0006);

    /* Read ABAR (BAR5, offset 0x24) */
    uint32_t abar_phys = pci_cfg_read32(ahci_bus, ahci_slot, ahci_func, 0x24) & ~0xFFF;
    abar = (hba_mem_t*)abar_phys;

    debug_print("[AHCI] ABAR physical base: ");
    debug_hex(abar_phys);
    debug_print("\n");

    /* Enable AHCI Mode in Global Host Control */
    abar->ghc |= HBA_GHC_AE;

    probe_ports(abar);
}

static uint64_t port_sector_counts[32] = {0};

static int ahci_identify_device(uint8_t port_no, void* buf) {
    if (!abar || !(ports_active_mask & (1 << port_no))) return -1;

    hba_port_t* port = &abar->ports[port_no];
    port->is = (uint32_t)-1;

    int slot = ahci_find_cmdslot(port);
    if (slot == -1) return -1;

    hba_cmd_header_t* cmd_header = (hba_cmd_header_t*)(port->clb);
    cmd_header += slot;
    cmd_header->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    cmd_header->w = 0;          /* Read operation */
    cmd_header->prdtl = 1;

    hba_cmd_table_t* cmd_tbl = (hba_cmd_table_t*)(cmd_header->ctba);
    uint8_t* p = (uint8_t*)cmd_tbl;
    for (uint32_t i = 0; i < sizeof(hba_cmd_table_t) + sizeof(hba_prdt_entry_t); i++) p[i] = 0;

    cmd_tbl->prdt_entry[0].dba = (uint32_t)buf;
    cmd_tbl->prdt_entry[0].dbau = 0;
    cmd_tbl->prdt_entry[0].dbc = 512 - 1; /* 512 bytes */
    cmd_tbl->prdt_entry[0].i = 1;

    fis_reg_h2d_t* cmdfis = (fis_reg_h2d_t*)(&cmd_tbl->cfis);
    cmdfis->fis_type = 0x27;    /* Register FIS - Host to Device */
    cmdfis->c = 1;
    cmdfis->command = ATA_CMD_IDENTIFY;
    cmdfis->device = 0;

    int spin = 0;
    while ((port->tfd & (0x80 | 0x08)) && spin < 1000000) spin++;
    if (spin == 1000000) return -2;

    port->ci = 1 << slot;

    while (1) {
        if ((port->ci & (1 << slot)) == 0) break;
        if (port->is & (1 << 30)) return -3;
    }

    if (port->is & (1 << 30)) return -3;
    return 0;
}

uint64_t ahci_get_sector_count(uint8_t port_no) {
    if (port_no >= 32) return 0;
    return port_sector_counts[port_no];
}

int ahci_is_drive_available(uint8_t port_no) {
    if (port_no >= 32) return 0;
    return (ports_active_mask & (1 << port_no)) != 0;
}
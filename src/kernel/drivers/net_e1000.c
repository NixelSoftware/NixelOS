#include "net_e1000.h"
#include "serial.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define INTEL_VENDOR_ID    0x8086

#define REG_CTRL      0x0000
#define REG_STATUS    0x0008
#define REG_EECD      0x0010
#define REG_EERD      0x0014
#define REG_ICR       0x00C0
#define REG_IMS       0x00D0
#define REG_IMC       0x00D8
#define REG_RCTL      0x0100
#define REG_TCTL      0x0400
#define REG_TIPG      0x0410
#define REG_RDBAL     0x2800
#define REG_RDBAH     0x2804
#define REG_RDLEN     0x2808
#define REG_RDH       0x2810
#define REG_RDT       0x2818
#define REG_TDBAL     0x3800
#define REG_TDBAH     0x3804
#define REG_TDLEN     0x3808
#define REG_TDH       0x3810
#define REG_TDT       0x3818
#define REG_MTA       0x5200
#define REG_RAL       0x5400
#define REG_RAH       0x5404

#define CTRL_SLU      (1 << 6)
#define CTRL_RST      (1 << 26)

#define RCTL_EN       (1 << 1)
#define RCTL_SBP      (1 << 2)
#define RCTL_UPE      (1 << 3)
#define RCTL_MPE      (1 << 4)
#define RCTL_BAM      (1 << 15)
#define RCTL_BSIZE_2K (0 << 16)
#define RCTL_SECRC    (1 << 26)

#define TCTL_EN       (1 << 1)
#define TCTL_PSP      (1 << 3)
#define TCTL_CT_SHIFT 4
#define TCTL_COLD_SHIFT 12

#define E1000_NUM_RX_DESC 16
#define E1000_NUM_TX_DESC 16
#define E1000_BUF_SIZE    2048

#define RX_STATUS_DD  (1 << 0)
#define RX_STATUS_EOP (1 << 1)
#define TX_CMD_EOP    (1 << 0)
#define TX_CMD_IFCS   (1 << 1)
#define TX_CMD_RS     (1 << 3)
#define TX_STATUS_DD  (1 << 0)
#define TX_STATUS_EC  (1 << 2)
#define TX_STATUS_LC  (1 << 3)
#define TX_STATUS_TU  (1 << 4)

typedef struct {
    uint64_t buffer_addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed, aligned(16))) e1000_rx_desc_t;

typedef struct {
    uint64_t buffer_addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed, aligned(16))) e1000_tx_desc_t;

static uint16_t e1000_io_base = 0;
static uint32_t e1000_mmio_base = 0;
static uint8_t  use_mmio = 0;
static uint8_t  ready = 0;
static uint64_t mac_addr = 0;
static uint32_t ip_addr = 0;
static net_receive_handler_t rx_handler = 0;
static net_diagnostics_t diag;

static uint16_t rx_cur = 0;
static uint16_t tx_cur = 0;
static uint8_t tx_pending[E1000_NUM_TX_DESC];

static e1000_rx_desc_t rx_descs[E1000_NUM_RX_DESC] __attribute__((aligned(16)));
static e1000_tx_desc_t tx_descs[E1000_NUM_TX_DESC] __attribute__((aligned(16)));
static uint8_t rx_buffers[E1000_NUM_RX_DESC][E1000_BUF_SIZE] __attribute__((aligned(16)));
static uint8_t tx_buffers[E1000_NUM_TX_DESC][E1000_BUF_SIZE] __attribute__((aligned(16)));

static uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t address = 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, val);
}

static inline void e1000_write(uint32_t reg, uint32_t val) {
    if (use_mmio && e1000_mmio_base != 0) {
        *((volatile uint32_t*)(e1000_mmio_base + reg)) = val;
    } else if (e1000_io_base != 0) {
        outl(e1000_io_base, reg);
        outl(e1000_io_base + 4, val);
    }
}

static inline uint32_t e1000_read(uint32_t reg) {
    if (use_mmio && e1000_mmio_base != 0) {
        return *((volatile uint32_t*)(e1000_mmio_base + reg));
    } else if (e1000_io_base != 0) {
        outl(e1000_io_base, reg);
        return inl(e1000_io_base + 4);
    }
    return 0xFFFFFFFF;
}

static uint16_t e1000_eeprom_read(uint8_t addr) {
    uint32_t tmp = 0;
    
    e1000_write(REG_EERD, 1 | ((uint32_t)addr << 8));
    for (int i = 0; i < 10000; i++) {
        tmp = e1000_read(REG_EERD);
        if (tmp & (1 << 4)) { 
            return (uint16_t)((tmp >> 16) & 0xFFFF);
        }
        __asm__ volatile ("pause");
    }
    return 0;
}

static int is_e1000_device(uint16_t dev_id) {
    switch (dev_id) {
        case 0x1000: case 0x1001: case 0x1004: case 0x1008:
        case 0x1009: case 0x100C: case 0x100D: case 0x100E:
        case 0x100F: case 0x1010: case 0x1011: case 0x1012:
        case 0x1015: case 0x1016: case 0x1017: case 0x1018:
        case 0x1019: case 0x101D: case 0x101E: case 0x1026:
        case 0x1079: case 0x107C: case 0x10D3:
            return 1;
        default:
            return 0;
    }
}

static int e1000_pci_find(void) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t id = pci_read((uint8_t)bus, slot, func, 0);
                uint16_t vendor = (uint16_t)id;
                uint16_t device = (uint16_t)(id >> 16);

                if (vendor == INTEL_VENDOR_ID && is_e1000_device(device)) {
                    
                    uint32_t cmd = pci_read((uint8_t)bus, slot, func, 0x04);
                    cmd |= 0x0007;
                    pci_write32((uint8_t)bus, slot, func, 0x04, cmd);

                    uint32_t bar0 = pci_read((uint8_t)bus, slot, func, 0x10);
                    uint32_t bar1 = pci_read((uint8_t)bus, slot, func, 0x14);

                    if ((bar0 & ~0xFU) == 0 && (bar1 & ~0x3U) == 0) {
                        diag.init_failure = NET_INIT_FAILURE_DISCOVERY;
                        return 0;
                    }

                    e1000_mmio_base = bar0 & ~0xF;
                    e1000_io_base = (uint16_t)(bar1 & ~0x3);

                    
                    use_mmio = 1;
                    uint32_t test_val = e1000_read(REG_STATUS);
                    if (test_val == 0 || test_val == 0xFFFFFFFF) {
                        
                        use_mmio = 0;
                        test_val = e1000_read(REG_STATUS);
                    }

                    debug_print("[E1000] Access Mode: ");
                    debug_print(use_mmio ? "MMIO (" : "IO Ports (");
                    debug_hex(use_mmio ? e1000_mmio_base : e1000_io_base);
                    debug_print(") Initial STATUS: ");
                    debug_hex(test_val);
                    debug_print("\n");

                    if (test_val == 0xFFFFFFFFU) {
                        diag.init_failure = NET_INIT_FAILURE_REGISTER_ACCESS;
                        diag.last_error_status = 0xFFFF;
                        diag.dw_csr0 = test_val;
                        return 0;
                    }

                    return 1;
                }
            }
        }
    }
    return 0;
}

uint8_t e1000_init(void) {
    ready = 0;
    for (uint32_t i = 0; i < sizeof(diag); i++) ((uint8_t*)&diag)[i] = 0;
    rx_cur = 0;
    tx_cur = 0;
    for (uint8_t i = 0; i < E1000_NUM_TX_DESC; i++) tx_pending[i] = 0;
    if (!e1000_pci_find()) {
        debug_print("[E1000] PCI device search failed.\n");
        if (diag.init_failure == NET_INIT_FAILURE_NONE) diag.init_failure = NET_INIT_FAILURE_DISCOVERY;
        return 0;
    }

    
    e1000_write(REG_CTRL, e1000_read(REG_CTRL) | CTRL_RST);
    for (volatile int i = 0; i < 50000; i++) __asm__ volatile ("pause");
    if (e1000_read(REG_CTRL) & CTRL_RST) {
        diag.init_failure = NET_INIT_FAILURE_TIMEOUT;
        diag.last_error_status = (uint16_t)e1000_read(REG_STATUS);
        diag.dw_csr0 = e1000_read(REG_STATUS);
        return 0;
    }

    
    e1000_write(REG_CTRL, CTRL_SLU | (1 << 5));

    
    e1000_write(REG_IMC, 0xFFFFFFFF);
    e1000_read(REG_ICR);

    
    uint16_t eeprom0 = e1000_eeprom_read(0);
    uint16_t eeprom1 = e1000_eeprom_read(1);
    uint16_t eeprom2 = e1000_eeprom_read(2);

    debug_print("[E1000] EEPROM: ");
    debug_hex(eeprom0); debug_print(" ");
    debug_hex(eeprom1); debug_print(" ");
    debug_hex(eeprom2); debug_print("\n");

    uint8_t mac_bytes[6];
    if (eeprom0 != 0 && eeprom0 != 0xFFFF) {
        mac_bytes[0] = (uint8_t)(eeprom0 & 0xFF);
        mac_bytes[1] = (uint8_t)((eeprom0 >> 8) & 0xFF);
        mac_bytes[2] = (uint8_t)(eeprom1 & 0xFF);
        mac_bytes[3] = (uint8_t)((eeprom1 >> 8) & 0xFF);
        mac_bytes[4] = (uint8_t)(eeprom2 & 0xFF);
        mac_bytes[5] = (uint8_t)((eeprom2 >> 8) & 0xFF);
    } else {
        
        uint32_t ral = e1000_read(REG_RAL);
        uint32_t rah = e1000_read(REG_RAH);
        mac_bytes[0] = (uint8_t)(ral & 0xFF);
        mac_bytes[1] = (uint8_t)((ral >> 8) & 0xFF);
        mac_bytes[2] = (uint8_t)((ral >> 16) & 0xFF);
        mac_bytes[3] = (uint8_t)((ral >> 24) & 0xFF);
        mac_bytes[4] = (uint8_t)(rah & 0xFF);
        mac_bytes[5] = (uint8_t)((rah >> 8) & 0xFF);
    }

    mac_addr = 0;
    for (int i = 0; i < 6; i++) {
        mac_addr = (mac_addr << 8) | mac_bytes[i];
    }

    debug_print("[E1000] Final MAC (hi/lo): ");
    debug_hex((uint32_t)(mac_addr >> 32));
    debug_hex((uint32_t)mac_addr);
    debug_print("\n");

    
    uint32_t write_ral = (uint32_t)mac_bytes[0] | ((uint32_t)mac_bytes[1] << 8) |
                         ((uint32_t)mac_bytes[2] << 16) | ((uint32_t)mac_bytes[3] << 24);
    uint32_t write_rah = (uint32_t)mac_bytes[4] | ((uint32_t)mac_bytes[5] << 8) | 0x80000000U;
    e1000_write(REG_RAL, write_ral);
    e1000_write(REG_RAH, write_rah);

    
    for (int i = 0; i < 128; i++) e1000_write(REG_MTA + (i * 4), 0);

    
    rx_cur = 0;
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        rx_descs[i].buffer_addr = (uint32_t)&rx_buffers[i][0];
        rx_descs[i].status = 0;
    }
    e1000_write(REG_RDBAL, (uint32_t)&rx_descs[0]);
    e1000_write(REG_RDBAH, 0);
    e1000_write(REG_RDLEN, E1000_NUM_RX_DESC * sizeof(e1000_rx_desc_t));
    e1000_write(REG_RDH, 0);
    e1000_write(REG_RDT, E1000_NUM_RX_DESC - 1);
    e1000_write(REG_RCTL, RCTL_EN | RCTL_SBP | RCTL_UPE | RCTL_MPE | RCTL_BAM | RCTL_BSIZE_2K | RCTL_SECRC);

    
    tx_cur = 0;
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        tx_descs[i].buffer_addr = (uint32_t)&tx_buffers[i][0];
        tx_descs[i].status = TX_STATUS_DD;
        tx_descs[i].cmd = 0;
    }
    e1000_write(REG_TDBAL, (uint32_t)&tx_descs[0]);
    e1000_write(REG_TDBAH, 0);
    e1000_write(REG_TDLEN, E1000_NUM_TX_DESC * sizeof(e1000_tx_desc_t));
    e1000_write(REG_TDH, 0);
    e1000_write(REG_TDT, 0);

    e1000_write(REG_TIPG, 0x0060200A);
    e1000_write(REG_TCTL, TCTL_EN | TCTL_PSP | (15 << TCTL_CT_SHIFT) | (64 << TCTL_COLD_SHIFT));

    uint32_t final_status = e1000_read(REG_STATUS);
    diag.dw_csr0 = final_status;
    diag.csr0 = (uint16_t)final_status;
    debug_print("[E1000] Final STATUS: ");
    debug_hex(final_status);
    debug_print("\n");

    if (final_status == 0xFFFFFFFFU) {
        diag.init_failure = NET_INIT_FAILURE_REGISTER_ACCESS;
        diag.last_error_status = 0xFFFF;
        return 0;
    }
    ready = 1;
    diag.initialized = 1;
    return 1;
}

uint8_t e1000_is_ready(void) {
    if (!ready) return 0;
    uint32_t status = e1000_read(REG_STATUS);
    
    return (status & 0x02) ? 1 : 0;
}

void e1000_shutdown(void) {
    if (!ready) return;
    e1000_write(REG_RCTL, 0);
    e1000_write(REG_TCTL, 0);
    e1000_write(REG_IMC, 0xFFFFFFFF);
    ready = 0;
}

uint8_t e1000_send(const uint8_t* data, uint32_t length) {
    diag.tx_attempts++;
    if (!ready || !data || length == 0 || length > NET_MAX_FRAME_SIZE || length > E1000_BUF_SIZE) return 0;

    e1000_tx_desc_t* desc = &tx_descs[tx_cur];
    if (!(desc->status & TX_STATUS_DD)) {
        diag.tx_busy++;
        return 0;
    }

    uint8_t* dst = (uint8_t*)(uint32_t)desc->buffer_addr;
    for (uint32_t i = 0; i < length; i++) dst[i] = data[i];

    desc->length = (uint16_t)length;
    desc->cmd = TX_CMD_EOP | TX_CMD_IFCS | TX_CMD_RS;
    desc->status = 0;
    tx_pending[tx_cur] = 1;
    diag.tx_submitted++;

    tx_cur = (tx_cur + 1) % E1000_NUM_TX_DESC;
    e1000_write(REG_TDT, tx_cur);

    return 1;
}

void e1000_poll(void) {
    if (!ready) return;

    for (uint8_t i = 0; i < E1000_NUM_TX_DESC; i++) {
        if (tx_pending[i] && (tx_descs[i].status & TX_STATUS_DD)) {
            tx_pending[i] = 0;
            diag.tx_completed++;
            diag.last_tx_status = tx_descs[i].status;
            if (tx_descs[i].status & (TX_STATUS_EC | TX_STATUS_LC | TX_STATUS_TU))
                diag.tx_errors++;
        }
    }

    /* Return to input/rendering even when the NIC keeps refilling the ring. */
    for (uint32_t budget = 8; budget && (rx_descs[rx_cur].status & RX_STATUS_DD); budget--) {
        e1000_rx_desc_t* desc = &rx_descs[rx_cur];
        uint16_t length = desc->length;
        diag.last_rx_length = length;
        diag.rx_descriptor_status = desc->status;
        diag.rx_message_length = length;

        if (desc->errors) {
            diag.rx_errors++;
        } else if ((desc->status & RX_STATUS_EOP) && length > 0 && length <= E1000_BUF_SIZE) {
            diag.rx_frames++;
            if (rx_handler) rx_handler((const uint8_t*)(uint32_t)desc->buffer_addr, length);
        } else {
            diag.rx_invalid++;
        }

        desc->status = 0;
        uint16_t old_cur = rx_cur;
        rx_cur = (rx_cur + 1) % E1000_NUM_RX_DESC;
        e1000_write(REG_RDT, old_cur);
    }
}

void e1000_set_receive_handler(net_receive_handler_t handler) {
    rx_handler = handler;
}

uint64_t e1000_get_mac_address(void) {
    return mac_addr;
}

void e1000_set_ip_address(uint32_t address) {
    ip_addr = address;
}

uint32_t e1000_get_ip_address(void) {
    return ip_addr;
}

void e1000_get_diagnostics(net_diagnostics_t* output) {
    if (!output) return;
    *output = diag;
    output->initialized = ready;
    output->io_base = e1000_io_base;
    output->tx_index = (uint8_t)tx_cur;
    output->rx_index = (uint8_t)rx_cur;
    output->tx_descriptor_address = (uint32_t)tx_descs[tx_cur].buffer_addr;
    output->tx_descriptor_length = (int16_t)tx_descs[tx_cur].length;
    output->tx_descriptor_status = tx_descs[tx_cur].status;
}

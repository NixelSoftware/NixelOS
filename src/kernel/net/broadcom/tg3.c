#include "tg3.h"
#include "../../drivers/serial.h"
#include "../../drivers/pci.h"
#include "../../drivers/delay.h"
#include "../../hal/net.h"
#include "tg3_regs.h"
#include "../phys/broadcom.h"

#define BROADCOM_VENDOR_ID          0x14E4

#define TG3_PCI_MISC_HOST_CTRL      0x00000068
#define TG3_MISC_HOST_CTRL_CLEAR_INT 0x00000001
#define TG3_MISC_HOST_CTRL_MASK_INT 0x00000002
#define TG3_DMA_RWCTRL              0x0000006C

#define TG3_GRC_MODE                0x00006800
#define TG3_GRC_MODE_HOST_SENDBDS   0x00020000
#define TG3_GRC_MODE_NO_TX_PHDR_CSUM 0x00100000

#define TG3_GRC_MISC_CFG            0x00006804
#define GRC_MISC_CFG_EPHY_IDDQ      0x00200000
#define GRC_MISC_CFG_KEEP_GPHY_PWR  0x04000000

#define TG3_MAC_MODE                0x00000400
#define TG3_MAC_MODE_PORT_GMII      0x00000008
#define TG3_MAC_STATUS              0x00000404

#define TG3_MAC_ADDR_0_HIGH         0x00000410
#define TG3_MAC_ADDR_0_LOW          0x00000414
#define TG3_MAC_RX_MTU_SIZE         0x0000043C

#define TG3_MAC_MI_COM              0x0000044C
#define TG3_MAC_MI_STAT             0x00000450
#define TG3_MAC_MI_MODE             0x00000454

#define TG3_MAC_TX_MODE             0x0000045C
#define TG3_MAC_TX_ENABLE           0x00000002
#define TG3_MAC_TX_LENGTHS          0x00000464
#define TG3_MAC_RX_MODE             0x00000468
#define TG3_MAC_RX_ENABLE           0x00000002
#define TG3_MAC_RX_PROMISC          0x00000100

#define TG3_MAC_HASH_0              0x00000470
#define TG3_MAC_HASH_1              0x00000474
#define TG3_MAC_HASH_2              0x00000478
#define TG3_MAC_HASH_3              0x0000047C

#define TG3_HOSTCC_MODE             0x00003C00
#define TG3_HOSTCC_MODE_ENABLE      0x00000002
#define TG3_HOSTCC_STATUS_BLK_HOST  0x00003C38

#define TG3_MEMARB_MODE             0x00004000
#define TG3_MEMARB_MODE_ENABLE      0x00000002

#define TG3_BUFMGR_MODE             0x00004400
#define TG3_BUFMGR_MODE_ENABLE      0x00000002

#define TG3_FTQ_RESET               0x00005C00

#define TG3_RCVDBDI_STD_BD          0x00002450

#define TG3_RCV_STD_PROD_IDX_MBOX   0x0000026C
#define TG3_RCV_RET_CONS_IDX_MBOX   0x00000284
#define TG3_SND_HOST_PROD_IDX_MBOX  0x00000304

#define MI_COM_CMD_WRITE            0x04000000
#define MI_COM_CMD_READ             0x08000000
#define MI_COM_READ_FAILED          0x10000000
#define MI_COM_START                0x20000000
#define MI_COM_BUSY                 0x20000000
#define MI_COM_PHY_ADDR_SHIFT       21
#define MI_COM_REG_ADDR_SHIFT       16
#define MI_COM_DATA_MASK            0x0000FFFF

#define MAC_MI_MODE_CLK_10MHZ       0x00000001
#define MAC_MI_MODE_AUTO_POLL       0x00000010
#define MAC_MI_MODE_BASE            0x000C0000

#define MII_BMCR                    0x00
#define MII_BMSR                    0x01
#define MII_PHYID1                  0x02
#define MII_PHYID2                  0x03
#define MII_ADVERTISE               0x04
#define MII_CTRL1000                0x09

#define BMCR_RESET                  0x8000
#define BMCR_PDOWN                  0x0800
#define BMCR_ANENABLE               0x1000
#define BMCR_ANRESTART              0x0200
#define BMCR_FULLDPLX               0x0100
#define BMCR_SPEED1000              0x0040
#define BMCR_SPEED100               0x2000

#define ADVERTISE_CSMA              0x0001
#define ADVERTISE_ALL               0x01E0
#define ADVERTISE_1000FULL          0x0200
#define ADVERTISE_1000HALF          0x0100

#define TG3_RX_RING_SIZE            512
#define TG3_TX_RING_SIZE            512
#define TG3_BUFFER_COUNT            8
#define TG3_TX_BUFFER_COUNT         8
#define TG3_BUF_SIZE                1536

#define TG3PCI_MEM_WIN_BASE_ADDR    0x0000007C
#define TG3PCI_MEM_WIN_DATA         0x00000084

#define TG3_RCVDBDI_MODE            0x00002400
#define  RCVDBDI_MODE_ENABLE        0x00000002
#define  RCVDBDI_MODE_INV_RING_SZ   0x00000010

#define TG3_WDMAC_MODE              0x00004C00
#define  WDMAC_MODE_ENABLE          0x00000002

#define TG3_RCVBDI_MODE             0x00002C00
#define  RCVBDI_MODE_ENABLE         0x00000002

#define TG3_RCVCC_MODE              0x00003000
#define  RCVCC_MODE_ENABLE          0x00000002

#define TG3_RCVLPC_MODE             0x00002000
#define  RCVLPC_MODE_ENABLE         0x00000002

/* DMA pipeline registers and descriptor flags (Tigon3 register interface). */
#define TG3_SNDDATAI_MODE           0x0C00
#define TG3_SNDDATAC_MODE           0x1000
#define TG3_SNDBDS_MODE             0x1400
#define TG3_SNDBDI_MODE             0x1800
#define TG3_SNDBDC_MODE             0x1C00
#define TG3_RCVDCC_MODE             0x2800
#define TG3_RDMAC_MODE              0x4800
#define TG3_RCVLPC_CONFIG           0x2010
#define TG3_MAC_RCV_RULE_CFG        0x0500
#define TG3_RCVBDI_STD_THRESH       0x2C18
#define TG3_RCVDBDI_JUMBO_FLAGS     0x2448
#define TG3_BUFMGR_RDMA_LOW_WATER   0x4410
#define TG3_BUFMGR_MACRX_LOW_WATER  0x4414
#define TG3_BUFMGR_HIGH_WATER       0x4418
#define TG3_BUFMGR_DMA_LOW_WATER    0x4434
#define TG3_BUFMGR_DMA_HIGH_WATER   0x4438
#define TG3_BLOCK_ENABLE           0x00000002
#define TG3_BD_DISABLED            0x00000002
#define TG3_RXD_END                0x00000004
#define TG3_RXD_ERROR              0x00000400
#define TG3_RXD_STD_RING           0x00010000
#define TG3_RXD_RING_MASK          0x00070000
#define TG3_TXD_END                0x00000004
#define TG3_HOSTCC_MODE_NOW        0x00000008
#define TG3_MAC_MODE_DMA_ENABLE    0x00600000
#define TG3_GRC_MODE_LE_DMA        0x00000034
#define TG3_GRC_MODE_STACK_UP      0x00010000

#pragma pack(push, 1)
typedef struct {
    uint32_t addr_hi;
    uint32_t addr_lo;
    uint32_t idx_len;
    uint32_t type_flags;
    uint32_t ip_tcp_csum;
    uint32_t err_vlan;
    uint32_t reserved;
    uint32_t opaque;
} tg3_rx_desc_t;

typedef struct {
    uint32_t addr_hi;
    uint32_t addr_lo;
    uint32_t len_flags;
    uint32_t vlan_tag;
} tg3_tx_desc_t;

typedef struct {
    uint32_t status;
    uint32_t status_tag;
    uint16_t rx_jumbo_consumer;
    uint16_t rx_consumer;
    uint16_t rx_mini_consumer;
    uint16_t reserved;
    uint16_t rx_producer;
    uint16_t tx_consumer;
    uint32_t other_ring_indices[15];
} tg3_status_block_t;
#pragma pack(pop)

_Static_assert(sizeof(tg3_rx_desc_t) == 32, "Tg3 RX DMA descriptor size");
_Static_assert(sizeof(tg3_tx_desc_t) == 16, "Tg3 TX DMA descriptor size");
_Static_assert(sizeof(tg3_status_block_t) == 80, "Tg3 status DMA size");
_Static_assert(__builtin_offsetof(tg3_rx_desc_t, opaque) == 28, "Tg3 RX opaque offset");
_Static_assert(__builtin_offsetof(tg3_status_block_t, rx_producer) == 16, "Tg3 RX status offset");
_Static_assert(__builtin_offsetof(tg3_status_block_t, tx_consumer) == 18, "Tg3 TX status offset");

static uint32_t tg3_mmio_base = 0;
static uint8_t  tg3_ready = 0;
static uint64_t tg3_mac_addr = 0;
static uint32_t tg3_ip_addr = 0;
static uint8_t  tg3_phy_addr = 1;
static uint16_t tg3_phy_id = 0;
static uint32_t tg3_phy_identifier;
static net_receive_handler_t tg3_rx_handler = 0;
static net_diagnostics_t tg3_diag;

static uint16_t tg3_tx_prod = 0;
static uint16_t tg3_rx_rcb_cons = 0;
static uint16_t tg3_rx_std_prod, tg3_tx_cons;
static uint16_t tg3_device;
static const pci_device_t* tg3_pci;
static uint32_t tg3_chip_revision, tg3_asic;
static uint8_t tg3_cpmu, tg3_57765, tg3_asf;
static uint32_t tg3_host_control;
static uint8_t tg3_link_up;
static uint32_t tg3_link_poll;

static tg3_rx_desc_t rx_std_ring[TG3_RX_RING_SIZE] __attribute__((aligned(64)));
static volatile tg3_rx_desc_t rx_ret_ring[TG3_RX_RING_SIZE] __attribute__((aligned(64)));
static tg3_tx_desc_t tx_ring[TG3_TX_RING_SIZE]     __attribute__((aligned(64)));
static volatile tg3_status_block_t status_block    __attribute__((aligned(64)));

/* Buffer pools are smaller than the hardware rings to fit kernel BSS. */
static uint8_t rx_buffers[TG3_BUFFER_COUNT][TG3_BUF_SIZE] __attribute__((aligned(64)));
static uint8_t tx_buffers[TG3_TX_BUFFER_COUNT][TG3_BUF_SIZE] __attribute__((aligned(64)));

#ifdef TG3_HOST_TEST
/* Register model for reset-sequence tests; absent from production builds. */
extern uint32_t tg3_test_read32(uint32_t reg);
extern void tg3_test_write32(uint32_t reg, uint32_t value);
extern void tg3_test_write_reset(uint32_t value);
static inline void tg3_write32(uint32_t reg, uint32_t value) { tg3_test_write32(reg, value); }
static inline uint32_t tg3_read32(uint32_t reg) { return tg3_test_read32(reg); }
#else
static inline void tg3_write32(uint32_t off, uint32_t val) {
    if (tg3_mmio_base) {
        __asm__ volatile ("" ::: "memory");
        *((volatile uint32_t*)(tg3_mmio_base + off)) = val;
        /* Drain posted PCI writes before a dependent register operation. */
        (void)*((volatile uint32_t*)(tg3_mmio_base + TG3_PCI_MISC_HOST_CTRL));
    }
}

static inline uint32_t tg3_read32(uint32_t off) {
    if (tg3_mmio_base) {
        return *((volatile uint32_t*)(tg3_mmio_base + off));
    }
    return 0xFFFFFFFF;
}

#endif
static void tg3_write_reset(uint32_t value) {
#ifdef TG3_HOST_TEST
    tg3_test_write_reset(value);
#else
    *((volatile uint32_t*)(tg3_mmio_base + TG3_GRC_MISC_CFG)) = value;
    __asm__ volatile ("" ::: "memory");
#endif
}

static inline void tg3_delay(void) {
    driver_delay_us(40);
}

/* Config-window SRAM access also works while the MMIO window is restored.
 * The network wrapper serializes ownership of this NIC's window. */
static void tg3_write_mem(uint32_t off, uint32_t value) {
    pci_write32(tg3_pci, TG3PCI_MEM_WIN_BASE_ADDR, off);
    pci_write32(tg3_pci, TG3PCI_MEM_WIN_DATA, value);
    pci_write32(tg3_pci, TG3PCI_MEM_WIN_BASE_ADDR, 0);
}
static uint32_t tg3_read_mem(uint32_t off) {
    pci_write32(tg3_pci, TG3PCI_MEM_WIN_BASE_ADDR, off);
    uint32_t value = pci_read32(tg3_pci, TG3PCI_MEM_WIN_DATA);
    pci_write32(tg3_pci, TG3PCI_MEM_WIN_BASE_ADDR, 0);
    return value;
}

static int is_tg3_device(uint16_t dev_id) {
    if (dev_id == 0x1673 || dev_id == 0x167A || dev_id == 0x1677 || dev_id == 0x167B) return 1;
    switch (dev_id) {
        case 0x1644: case 0x1645: case 0x1646: case 0x1647:
        case 0x1648: case 0x1649: case 0x1653: case 0x1654:
        case 0x1659: case 0x165A: case 0x165B: case 0x165D:
        case 0x165E: case 0x165F: case 0x1665: case 0x1668:
        case 0x1669: case 0x166A: case 0x1672: case 0x1674:
        case 0x1678: case 0x1679: case 0x1680: case 0x1681:
        case 0x1682: case 0x1684: case 0x1688: case 0x1689:
        case 0x1690: case 0x1691: case 0x1692: case 0x1693:
        case 0x1694: case 0x1698: case 0x169B: case 0x16A0:
        case 0x16B0: case 0x16B1: case 0x16B4: case 0x16B5:
        case 0x16FD: case 0x16FE:
            return 1;
        default:
            return 0;
    }
}

static int tg3_pci_find(void) {
    /* Use the function selected by the network layer, never rescan a different
     * Broadcom adapter (important on systems with several NICs). */
    tg3_pci = sys_net_pci_device();
    if (!tg3_pci || tg3_pci->vendor_id != BROADCOM_VENDOR_ID ||
        !is_tg3_device(tg3_pci->device_id) || tg3_pci->header_type != 0) return 0;
    if (!pci_enable_device(tg3_pci, PCI_COMMAND_MEMORY)) return 0;
    pci_set_master(tg3_pci, 0);
    pci_bar_t bar;
    if (!pci_probe_bar(tg3_pci, 0, &bar) || bar.io || !bar.address ||
        bar.address > 0xFFFFFFFFu || bar.size < 0x8000 ||
        bar.address + 0x8000 > 0x100000000ULL) return 0;
    tg3_mmio_base = (uint32_t)bar.address;
    tg3_device = tg3_pci->device_id;
    pci_prepare_polling(tg3_pci);
    /* Match Linux's register byte order before the first MMIO access. */
    uint32_t control = pci_read32(tg3_pci, TG3PCI_MISC_HOST_CTRL);
    if (control == 0xFFFFFFFF) return 0;
    tg3_chip_revision = control >> 16;
    tg3_cpmu = (tg3_chip_revision >> 12) == 0xF;
    if (tg3_cpmu) {
        uint16_t reg = (tg3_device & 0xFFF0) == 0x16B0 ?
            TG3PCI_GEN15_PRODID_ASICREV : TG3PCI_PRODID_ASICREV;
        tg3_chip_revision = pci_read32(tg3_pci, reg);
    }
    if (tg3_chip_revision == 0x5000) tg3_chip_revision = 0x6000;
    tg3_asic = tg3_chip_revision >> 12;
    tg3_57765 = tg3_asic == 0x57785 || tg3_asic == 0x57766;
    tg3_host_control = (control & 0xFFFF0000u) | 0x9Au;
    pci_write32(tg3_pci, TG3PCI_MISC_HOST_CTRL, tg3_host_control);
    return 1;
}

static int tg3_valid_mac(uint64_t mac) {
    return mac && mac != 0xFFFFFFFFFFFFULL && !(mac & 0x010000000000ULL);
}
static void tg3_read_mac(void) {
    uint32_t hi = tg3_read_mem(NIC_SRAM_MAC_ADDR_HIGH_MBOX);
    uint32_t lo = tg3_read_mem(NIC_SRAM_MAC_ADDR_LOW_MBOX);
    uint64_t mailbox_mac = ((uint64_t)(hi & 0xFFFF) << 32) | lo;
    if ((hi >> 16) == 0x484B && tg3_valid_mac(mailbox_mac)) {
        tg3_mac_addr = mailbox_mac;
        return;
    }
    hi = tg3_read32(TG3_MAC_ADDR_0_HIGH);
    lo = tg3_read32(TG3_MAC_ADDR_0_LOW);

    uint8_t mac[6];
    mac[0] = (uint8_t)(hi >> 8);
    mac[1] = (uint8_t)(hi & 0xFF);
    mac[2] = (uint8_t)(lo >> 24);
    mac[3] = (uint8_t)(lo >> 16);
    mac[4] = (uint8_t)(lo >> 8);
    mac[5] = (uint8_t)(lo & 0xFF);

    tg3_mac_addr = 0;
    for (int i = 0; i < 6; i++) {
        tg3_mac_addr = (tg3_mac_addr << 8) | mac[i];
    }
}

static int tg3_mdio_read(uint8_t phy_addr, uint8_t reg, uint32_t* val) {

    uint32_t orig_mi_mode = tg3_read32(TG3_MAC_MI_MODE);
    if (orig_mi_mode & MAC_MI_MODE_AUTO_POLL) {
        tg3_write32(TG3_MAC_MI_MODE, orig_mi_mode & ~MAC_MI_MODE_AUTO_POLL);
        tg3_delay();
    }

    int idle;
    for (idle = 0; idle < 10000; idle++) {
        if (!(tg3_read32(TG3_MAC_MI_COM) & MI_COM_BUSY)) break;
        driver_delay_us(10);
    }
    if (idle == 10000) {
        tg3_write32(TG3_MAC_MI_MODE, orig_mi_mode);
        return 0;
    }

    uint32_t frame = ((uint32_t)(phy_addr & 0x1F) << MI_COM_PHY_ADDR_SHIFT) |
                     ((uint32_t)(reg & 0x1F) << MI_COM_REG_ADDR_SHIFT) |
                     MI_COM_CMD_READ |
                     MI_COM_START;
    tg3_write32(TG3_MAC_MI_COM, frame);

    int ok = 0;
    for (int i = 0; i < 10000; i++) {
        uint32_t check = tg3_read32(TG3_MAC_MI_COM);
        if (!(check & MI_COM_BUSY)) {
            *val = check & MI_COM_DATA_MASK;
            ok = !(check & MI_COM_READ_FAILED);
            break;
        }
        driver_delay_us(10);
    }

    if (orig_mi_mode & MAC_MI_MODE_AUTO_POLL) {
        tg3_write32(TG3_MAC_MI_MODE, orig_mi_mode);
        tg3_delay();
    }

    return ok;
}

static int tg3_mdio_write(uint8_t phy_addr, uint8_t reg, uint32_t val) {
    uint32_t orig_mi_mode = tg3_read32(TG3_MAC_MI_MODE);
    if (orig_mi_mode & MAC_MI_MODE_AUTO_POLL) {
        tg3_write32(TG3_MAC_MI_MODE, orig_mi_mode & ~MAC_MI_MODE_AUTO_POLL);
        tg3_delay();
    }

    int idle;
    for (idle = 0; idle < 10000; idle++) {
        if (!(tg3_read32(TG3_MAC_MI_COM) & MI_COM_BUSY)) break;
        driver_delay_us(10);
    }
    if (idle == 10000) {
        tg3_write32(TG3_MAC_MI_MODE, orig_mi_mode);
        return 0;
    }

    uint32_t frame = ((uint32_t)(phy_addr & 0x1F) << MI_COM_PHY_ADDR_SHIFT) |
                     ((uint32_t)(reg & 0x1F) << MI_COM_REG_ADDR_SHIFT) |
                     (val & MI_COM_DATA_MASK) |
                     MI_COM_CMD_WRITE |
                     MI_COM_START;
    tg3_write32(TG3_MAC_MI_COM, frame);

    int ok = 0;
    for (int i = 0; i < 10000; i++) {
        uint32_t check = tg3_read32(TG3_MAC_MI_COM);
        if (!(check & MI_COM_BUSY)) {
            ok = 1;
            break;
        }
        driver_delay_us(10);
    }

    if (orig_mi_mode & MAC_MI_MODE_AUTO_POLL) {
        tg3_write32(TG3_MAC_MI_MODE, orig_mi_mode);
        tg3_delay();
    }

    return ok;
}

/* Linux tg3_stop_fw / tg3_chip_reset / tg3_poll_fw ordering, adapted to
 * Nixel's one polling queue and identity-mapped coherent DMA buffers. */
static int tg3_wait_fw_event(void) {
    for (uint32_t i = 0; i < 2500; i++) {
        if (!(tg3_read32(GRC_RX_CPU_EVENT) & GRC_RX_CPU_DRIVER_EVENT)) return 1;
        driver_delay_us(8);
    }
    return 0;
}
static int tg3_reset_chip(void) {
    pci_saved_state_t saved;
    uint32_t signature = tg3_read_mem(NIC_SRAM_DATA_SIG);
    uint32_t config = signature == NIC_SRAM_DATA_SIG_MAGIC ?
        tg3_read_mem(NIC_SRAM_DATA_CFG) : 0;
    /* APE-managed devices require BAR2 and APE locking, not ASF commands. */
    if (config & NIC_SRAM_DATA_CFG_APE_ENABLE) {
        debug_print("[TG3] APE management requires an APE backend\n");
        return 0;
    }
    tg3_asf = !!(config & NIC_SRAM_DATA_CFG_ASF_ENABLE);
    if (tg3_asf) {
        if (!tg3_wait_fw_event()) return 0;
        tg3_write_mem(NIC_SRAM_FW_CMD_MBOX, FWCMD_NICDRV_PAUSE_FW);
        tg3_write32(GRC_RX_CPU_EVENT, tg3_read32(GRC_RX_CPU_EVENT) | GRC_RX_CPU_DRIVER_EVENT);
        if (!tg3_wait_fw_event()) return 0;
    }
    /* Linux arbitrates NVRAM before a core reset, which releases the grant. */
    tg3_write32(NVRAM_SWARB, SWARB_REQ_SET1);
    uint32_t count;
    for (count = 0; count < 8000; count++) {
        if (tg3_read32(NVRAM_SWARB) & SWARB_GNT1) break;
        driver_delay_us(20);
    }
    if (count == 8000) {
        tg3_write32(NVRAM_SWARB, SWARB_REQ_CLR1);
        return 0;
    }
    uint8_t pcie = pci_find_capability(tg3_pci, PCI_CAP_PCIE);
    if (pcie && pcie <= 0xEC) {
        uint16_t ctl = pci_read16(tg3_pci, pcie + 8);
        ctl &= ~(0x10u | 0x800u); /* relaxed ordering, no-snoop */
        if (!tg3_cpmu) ctl &= ~0xE0u; /* older chips require 128-byte MPS */
        pci_write16(tg3_pci, pcie + 8, ctl);
    }
    if (!pci_save_state(tg3_pci, &saved)) {
        tg3_write32(NVRAM_SWARB, SWARB_REQ_CLR1);
        return 0;
    }
    tg3_write_mem(NIC_SRAM_FIRMWARE_MBOX, NIC_SRAM_FIRMWARE_MBOX_MAGIC1);
    if (tg3_asf) tg3_write_mem(NIC_SRAM_FW_DRV_STATE_MBOX, DRV_STATE_START);
    if (tg3_asic == 6 || tg3_asic == 0xA || tg3_asic == 0xB || tg3_cpmu)
        tg3_write32(GRC_FASTBOOT_PC, 0);
    if (tg3_asic == 0x57780) {
        uint32_t val = tg3_read32(TG3_PCIE_LNKCTL);
        tg3_write32(TG3_PCIE_LNKCTL, (val & ~TG3_PCIE_LNKCTL_L1_PLL_PD_EN) | TG3_PCIE_LNKCTL_L1_PLL_PD_DIS);
    }
    if (pcie && !tg3_57765 && tg3_asic != 0x5785 &&
        tg3_read32(TG3_PCIE_PHY_TSTCTL) == 0x60)
        tg3_write32(TG3_PCIE_PHY_TSTCTL, TG3_PCIE_PHY_TSTCTL_PSCRAM);
    uint32_t reset = GRC_MISC_CFG_CORECLK_RESET;
    if (pcie && tg3_chip_revision != 0x4000) {
        tg3_write32(TG3_GRC_MISC_CFG, 1u << 29);
        reset |= 1u << 29;
    }
    if (!tg3_cpmu) reset |= GRC_MISC_CFG_KEEP_GPHY_PWR;
    /* Crucial Linux rule: no MMIO flush/read immediately after core reset.
     * The memory-enable bit may disappear while the reset write is posted. */
    tg3_write_reset(reset);
    driver_delay_us(120);
    (void)pci_read16(tg3_pci, 4);
    driver_delay_us(120);
    if (tg3_chip_revision == 0x4000) driver_delay_us(500000);
    if (!pci_restore_state(tg3_pci, &saved)) return 0;
    pci_write32(tg3_pci, TG3PCI_MISC_HOST_CTRL, tg3_host_control);
    pci_prepare_polling(tg3_pci);
    if (pcie && pcie <= 0xEC) pci_write16(tg3_pci, pcie + 10, 0xF);
    tg3_write32(TG3_MEMARB_MODE, TG3_MEMARB_MODE_ENABLE);
    for (count = 0; count < 100000; count++) {
        if (tg3_read_mem(NIC_SRAM_FIRMWARE_MBOX) == ~(uint32_t)NIC_SRAM_FIRMWARE_MBOX_MAGIC1) break;
        if (pci_read16(tg3_pci, 0) != tg3_pci->vendor_id) return 0;
        driver_delay_us(10);
    }
    if (count == 100000) debug_print("[TG3] No boot firmware acknowledgement; continuing without firmware\n");
    if (tg3_chip_revision == 0x57785000) driver_delay_us(10000);
    /* Restore the address saved before reset, even if reset cleared MAC regs. */
    tg3_write32(TG3_MAC_ADDR_0_HIGH, (uint32_t)(tg3_mac_addr >> 32));
    tg3_write32(TG3_MAC_ADDR_0_LOW, (uint32_t)tg3_mac_addr);
    if (tg3_asf) tg3_write_mem(NIC_SRAM_FW_DRV_STATE_MBOX, DRV_STATE_START);
    return 1;
}

/* BCM57780 power/PCIe/DMA workarounds from Linux tg3_reset_hw(). */
static void tg3_apply_57780_quirks(void) {
    if (tg3_asic != 0x57780) return;
    uint32_t value = tg3_read32(PCIE_PWR_MGMT_THRESH);
    tg3_write32(PCIE_PWR_MGMT_THRESH, (value & ~PCIE_PWR_MGMT_L1_THRESH_MSK) |
                PCIE_PWR_MGMT_EXT_ASPM_TMR_EN | PCIE_PWR_MGMT_L1_THRESH_4MS);
    value = tg3_read32(TG3_PCIE_EIDLE_DELAY);
    tg3_write32(TG3_PCIE_EIDLE_DELAY, (value & ~TG3_PCIE_EIDLE_DELAY_MASK) |
                TG3_PCIE_EIDLE_DELAY_13_CLKS);
    tg3_write32(TG3_CORR_ERR_STAT, TG3_CORR_ERR_STAT_CLEAR);
    value = tg3_read32(TG3_PCIE_LNKCTL);
    tg3_write32(TG3_PCIE_LNKCTL, (value & ~TG3_PCIE_LNKCTL_L1_PLL_PD_EN) |
                TG3_PCIE_LNKCTL_L1_PLL_PD_DIS);
    value = tg3_read32(TG3_RDMA_RSRVCTRL_REG);
    tg3_write32(TG3_RDMA_RSRVCTRL_REG, value | TG3_RDMA_RSRVCTRL_FIFO_OFLW_FIX);
    /* Preserve firmware GPIO state on the onboard NIC, switch off debug UART. */
    value = tg3_read32(GRC_LOCAL_CTRL);
    tg3_write32(GRC_LOCAL_CTRL, value | GRC_LCLCTRL_GPIO_UART_SEL);
}
static int tg3_phy_read(void* context, uint8_t reg, uint16_t* value) {
    (void)context;
    uint32_t data;
    if (!tg3_mdio_read(tg3_phy_addr, reg, &data)) return 0;
    *value = data;
    return 1;
}
static int tg3_phy_write(void* context, uint8_t reg, uint16_t value) {
    (void)context;
    return tg3_mdio_write(tg3_phy_addr, reg, value);
}
static uint32_t tg3_mdio_mode(void) {
    /* CPMU parts, including BCM57780, need the constant 500 kHz clock. */
    return tg3_cpmu ? MAC_MI_MODE_500KHZ_CONST : MAC_MI_MODE_BASE;
}

static int tg3_phy_init(void) {
    uint32_t val = 0;

    tg3_write32(TG3_MAC_MI_MODE, tg3_mdio_mode());
    driver_delay_us(80);

    /* Linux powers the preferred PHY before probing its ID. */
    uint32_t control;
    if (tg3_mdio_read(1, MII_BMCR, &control) && (control & BMCR_PDOWN)) {
        if (!tg3_mdio_write(1, MII_BMCR, control & ~BMCR_PDOWN)) return 0;
        driver_delay_us(1000);
    }
    tg3_phy_addr = 1;
    int found = 0;
    for (uint8_t i = 0; i < 32; i++) {
        uint8_t a = i == 0 ? 1 : i == 1 ? 0 : i;
        uint32_t low;
        val = 0;
        if (tg3_mdio_read(a, MII_PHYID1, &val) && val != 0x0000 && val != 0xFFFF &&
            tg3_mdio_read(a, MII_PHYID2, &low) && low != 0xFFFF) {
            tg3_phy_identifier = (val << 16) | low;
            tg3_phy_addr = a;
            found = 1;
            break;
        }
    }

    if (!found) return 0;
    tg3_phy_id = (uint16_t)val;

    if (!tg3_mdio_write(tg3_phy_addr, MII_BMCR, BMCR_RESET)) return 0;
    int reset_done = 0;
    for (int i = 0; i < 500; i++) {
        if (!tg3_mdio_read(tg3_phy_addr, MII_BMCR, &val)) return 0;
        if (!(val & BMCR_RESET)) {
            reset_done = 1;
            break;
        }
        driver_delay_us(1000);
    }

    if (!reset_done || !tg3_mdio_read(tg3_phy_addr, MII_BMCR, &val)) return 0;
    val &= ~(BMCR_PDOWN | 0x0400u); /* Also clear MII isolate. */
    if (!tg3_mdio_write(tg3_phy_addr, MII_BMCR, val)) return 0;

    if (tg3_asic == 0x57780) {
        const bcm_mdio_t bus = {0, tg3_phy_read, tg3_phy_write};
        if (!bcm57780_phy_configure(&bus, tg3_phy_identifier)) {
            debug_print("[TG3] BCM57780 PHY setup failed; PHY ID: ");
            debug_hex(tg3_phy_identifier);
            debug_print("\n");
            return 0;
        }
    }

    if (!tg3_mdio_write(tg3_phy_addr, MII_ADVERTISE, ADVERTISE_CSMA | ADVERTISE_ALL) ||
        !tg3_mdio_write(tg3_phy_addr, MII_CTRL1000, ADVERTISE_1000FULL) ||
        !tg3_mdio_write(tg3_phy_addr, MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART))
        return 0;

    /* Poll copper link explicitly instead of racing firmware autopoll. */
    tg3_write32(TG3_MAC_MI_MODE, tg3_mdio_mode());
    tg3_delay();
    return 1;
}

uint8_t tg3_init(void) {
    if (tg3_mmio_base) tg3_shutdown();
    tg3_ready = 0;
    tg3_tx_prod = 0;
    tg3_rx_rcb_cons = 0;
    tg3_rx_std_prod = TG3_BUFFER_COUNT;
    tg3_tx_cons = 0;
    tg3_link_up = 0;
    tg3_link_poll = 0;
    tg3_mmio_base = 0;
    tg3_pci = 0;
    tg3_asf = 0;
    tg3_phy_identifier = 0;

    for (uint32_t i = 0; i < sizeof(tg3_diag); i++) {
        ((uint8_t*)&tg3_diag)[i] = 0;
    }

    if (!tg3_pci_find()) {
        if (tg3_pci) pci_disable_device(tg3_pci);
        tg3_diag.init_failure = NET_INIT_FAILURE_DISCOVERY;
        return 0;
    }

    debug_print("[TG3] Experimental PCIe copper driver, BAR: ");
    debug_hex(tg3_mmio_base);
    debug_print(" chip revision: ");
    debug_hex(tg3_chip_revision);
    debug_print("\n");
    if (tg3_read32(TG3_PCI_MISC_HOST_CTRL) == 0xFFFFFFFF) {
        tg3_diag.init_failure = NET_INIT_FAILURE_REGISTER_ACCESS;
        pci_disable_device(tg3_pci);
        return 0;
    }
    tg3_read_mac();
    if (!tg3_valid_mac(tg3_mac_addr)) {
        tg3_diag.init_failure = NET_INIT_FAILURE_REGISTER_ACCESS;
        pci_disable_device(tg3_pci);
        return 0;
    }
    tg3_shutdown();
    if (tg3_diag.init_failure == NET_INIT_FAILURE_TIMEOUT || !tg3_reset_chip()) {
        tg3_diag.init_failure = NET_INIT_FAILURE_TIMEOUT;
        pci_disable_device(tg3_pci);
        return 0;
    }
    tg3_apply_57780_quirks();
    uint32_t misc_cfg = tg3_read32(TG3_GRC_MISC_CFG);
    misc_cfg &= ~(GRC_MISC_CFG_EPHY_IDDQ | 0xFFu);
    misc_cfg |= 65u << 1; /* Linux's 66 MHz timer prescaler */
    if (!tg3_cpmu) misc_cfg |= GRC_MISC_CFG_KEEP_GPHY_PWR;
    tg3_write32(TG3_GRC_MISC_CFG, misc_cfg);

    /* PCI memory-read (6) / memory-write (7) commands, plus PCIe watermark. */
    tg3_write32(TG3_DMA_RWCTRL, tg3_57765 ? 1 : 0x76180000);

    uint32_t grc_mode = tg3_read32(TG3_GRC_MODE);
    /* Little-endian host descriptors, packet bytes in network order. */
    grc_mode &= ~(0x2u | 0x20000000u);
    grc_mode |= TG3_GRC_MODE_LE_DMA | TG3_GRC_MODE_STACK_UP | TG3_GRC_MODE_HOST_SENDBDS | TG3_GRC_MODE_NO_TX_PHDR_CSUM;
    tg3_write32(TG3_GRC_MODE, grc_mode);

    tg3_write32(TG3_HOSTCC_MODE, 0);
    tg3_write32(TG3_MEMARB_MODE, TG3_MEMARB_MODE_ENABLE);
    /* Standard-MTU watermarks for the selected PCIe families. */
    uint8_t newer = tg3_57765;
    tg3_write32(TG3_BUFMGR_RDMA_LOW_WATER, 0);
    tg3_write32(TG3_BUFMGR_MACRX_LOW_WATER, newer ? 0x2A : 0x10);
    tg3_write32(TG3_BUFMGR_HIGH_WATER, newer ? 0xA0 : 0x60);
    tg3_write32(TG3_BUFMGR_DMA_LOW_WATER, 5);
    tg3_write32(TG3_BUFMGR_DMA_HIGH_WATER, 10);
    tg3_write32(TG3_BUFMGR_MODE, TG3_BUFMGR_MODE_ENABLE);
    tg3_write32(TG3_FTQ_RESET, 0xFFFFFFFF);
    tg3_write32(TG3_FTQ_RESET, 0x00000000);

    for (uint32_t i = 0; i < sizeof(tg3_status_block_t) / 4; i++) {
        ((volatile uint32_t*)&status_block)[i] = 0;
    }
    tg3_write32(TG3_HOSTCC_STATUS_BLK_HOST + 0, 0);
    tg3_write32(TG3_HOSTCC_STATUS_BLK_HOST + 4, (uint32_t)&status_block);

    for (int i = 0; i < TG3_RX_RING_SIZE; i++) {
        rx_std_ring[i].addr_hi = 0;
        rx_std_ring[i].addr_lo = i < TG3_BUFFER_COUNT ? (uint32_t)rx_buffers[i] : 0;
        rx_std_ring[i].idx_len = (i << 16) | TG3_BUF_SIZE;
        rx_std_ring[i].type_flags = TG3_RXD_END;
        rx_std_ring[i].ip_tcp_csum = 0;
        rx_std_ring[i].err_vlan = 0;
        rx_std_ring[i].reserved = 0;
        rx_std_ring[i].opaque = TG3_RXD_STD_RING | i;
    }

    for (int i = 0; i < TG3_RX_RING_SIZE; i++) {
        rx_ret_ring[i].addr_hi = 0;
        rx_ret_ring[i].addr_lo = 0;
        rx_ret_ring[i].idx_len = 0;
        rx_ret_ring[i].type_flags = 0;
        rx_ret_ring[i].ip_tcp_csum = 0;
        rx_ret_ring[i].err_vlan = 0;
        rx_ret_ring[i].reserved = 0;
        rx_ret_ring[i].opaque = 0;
    }

    tg3_write32(TG3_RCVDBDI_STD_BD + 0x00, 0);
    tg3_write32(TG3_RCVDBDI_STD_BD + 0x04, (uint32_t)&rx_std_ring[0]);
    /* 57765 class encodes both ring count and buffer size. */
    uint32_t std_config = tg3_57765 ?
        (TG3_RX_RING_SIZE << 16) | (TG3_BUF_SIZE << 2) : TG3_RX_RING_SIZE << 16;
    tg3_write32(TG3_RCVDBDI_STD_BD + 0x08, std_config);
    tg3_write32(TG3_RCVDBDI_JUMBO_FLAGS, TG3_BD_DISABLED); /* Disable unused jumbo ring. */
    tg3_write32(TG3_RCVDBDI_STD_BD + 0x0C, 0x6000);

    /* Disable host rings left behind by firmware or an earlier OS. */
    for (uint32_t i = 1; i < 16; i++) {
        tg3_write_mem(0x100 + i * 16 + 8, 2);
        tg3_write_mem(0x200 + i * 16 + 8, 2);
    }
    tg3_write_mem(0x200 + 0x00, 0);
    tg3_write_mem(0x200 + 0x04, (uint32_t)&rx_ret_ring[0]);
    tg3_write_mem(0x200 + 0x08, (TG3_RX_RING_SIZE << 16));
    tg3_write_mem(0x200 + 0x0C, 0x0000);

    for (int i = 0; i < TG3_TX_RING_SIZE; i++) {
        tx_ring[i].addr_hi = 0;
        tx_ring[i].addr_lo = 0;
        tx_ring[i].len_flags = 0;
        tx_ring[i].vlan_tag = 0;
    }
    tg3_write_mem(0x100 + 0x00, 0);
    tg3_write_mem(0x100 + 0x04, (uint32_t)&tx_ring[0]);
    tg3_write_mem(0x100 + 0x08, (TG3_TX_RING_SIZE << 16));
    tg3_write_mem(0x100 + 0x0C, 0x4000);

    tg3_write32(TG3_RCV_STD_PROD_IDX_MBOX, tg3_rx_std_prod);
    tg3_write32(TG3_RCV_RET_CONS_IDX_MBOX, 0);
    tg3_write32(TG3_SND_HOST_PROD_IDX_MBOX, 0);

    /* Receive placement and all TX/RX DMA pipeline stages are required. */
    tg3_write32(TG3_RCVLPC_CONFIG, 0x181);
    tg3_write32(TG3_MAC_RCV_RULE_CFG, 0x08);
    tg3_write32(TG3_RCVBDI_STD_THRESH, TG3_BUFFER_COUNT / 4);
    uint32_t rdma = 0x3FEu | RDMAC_MODE_FIFO_LONG_BURST;
    if (tg3_asic == 0x57780) rdma |= RDMAC_MODE_BD_SBD_CRPT_ENAB |
        RDMAC_MODE_MBUF_RBD_CRPT_ENAB | RDMAC_MODE_MBUF_SBD_CRPT_ENAB;
    tg3_write32(TG3_RDMAC_MODE, rdma);
    tg3_write32(TG3_RCVDCC_MODE, TG3_BLOCK_ENABLE); /* Receive data completion */
    tg3_write32(TG3_SNDDATAI_MODE, TG3_BLOCK_ENABLE); /* Send data initiator */
    tg3_write32(TG3_SNDDATAC_MODE, TG3_BLOCK_ENABLE); /* Send data completion */
    tg3_write32(TG3_SNDBDS_MODE, TG3_BLOCK_ENABLE); /* Send BD selector */
    tg3_write32(TG3_SNDBDI_MODE, TG3_BLOCK_ENABLE); /* Send BD initiator */
    tg3_write32(TG3_SNDBDC_MODE, TG3_BLOCK_ENABLE); /* Send BD completion */
    uint32_t wdma = 0x3FEu;
    if (tg3_asic == 0x57780 || tg3_asic == 0xA || tg3_asic == 0xB || tg3_57765)
        wdma |= WDMAC_MODE_STATUS_TAG_FIX;
    tg3_write32(TG3_WDMAC_MODE, wdma);
    tg3_write32(TG3_RCVLPC_MODE, RCVLPC_MODE_ENABLE);
    tg3_write32(TG3_RCVCC_MODE, RCVCC_MODE_ENABLE);
    tg3_write32(TG3_RCVBDI_MODE, RCVBDI_MODE_ENABLE);
    tg3_write32(TG3_RCVDBDI_MODE, RCVDBDI_MODE_ENABLE | RCVDBDI_MODE_INV_RING_SZ);

    tg3_write32(TG3_MAC_HASH_0, 0xFFFFFFFF);
    tg3_write32(TG3_MAC_HASH_1, 0xFFFFFFFF);
    tg3_write32(TG3_MAC_HASH_2, 0xFFFFFFFF);
    tg3_write32(TG3_MAC_HASH_3, 0xFFFFFFFF);

    tg3_write32(TG3_MAC_RX_MTU_SIZE, 1536);
    tg3_write32(TG3_MAC_TX_LENGTHS, 0x00000020 | (0x06 << 12) | (0x02 << 16));
    tg3_write32(TG3_MAC_RX_MODE, TG3_MAC_RX_ENABLE | TG3_MAC_RX_PROMISC);
    tg3_write32(TG3_MAC_TX_MODE, TG3_MAC_TX_ENABLE);
    tg3_write32(TG3_MAC_MODE, TG3_MAC_MODE_PORT_GMII | TG3_MAC_MODE_DMA_ENABLE | MAC_MODE_FHDE_ENABLE);

    if (!tg3_phy_init()) {
        tg3_diag.init_failure = NET_INIT_FAILURE_TIMEOUT;
        /* Teardown must also work after partially completed initialization. */
        tg3_shutdown();
        pci_disable_device(tg3_pci);
        return 0;
    }

    tg3_write32(0x00003C08, 1);
    tg3_write32(0x00003C10, 1);
    tg3_write32(0x00003C0C, 1);
    tg3_write32(0x00003C14, 1);
    pci_set_master(tg3_pci, 1);
    tg3_write32(TG3_HOSTCC_MODE, TG3_HOSTCC_MODE_ENABLE);
    if (tg3_asf) tg3_write_mem(NIC_SRAM_FW_DRV_STATE_MBOX, DRV_STATE_START_DONE);

    debug_print("[TG3] DMA rings configured; PHY: ");
    debug_hex(tg3_phy_addr);
    debug_print(" ID: ");
    debug_hex(tg3_phy_identifier);
    debug_print("\n");
    tg3_ready = 1;
    tg3_diag.initialized = 1;
    return 1;
}

/* MAC mode follows negotiated copper speed/duplex, not the link-change bit. */
static void tg3_update_link(void) {
    uint32_t status, partner, gigabit, control, local, local_gigabit;
    if (!tg3_mdio_read(tg3_phy_addr, MII_BMSR, &status) ||
        !tg3_mdio_read(tg3_phy_addr, MII_BMSR, &status) ||
        !(status & 4) || !(status & 0x20)) {
        tg3_link_up = 0;
        return;
    }
    if (!tg3_mdio_read(tg3_phy_addr, 10, &gigabit) ||
        !tg3_mdio_read(tg3_phy_addr, 5, &partner) ||
        !tg3_mdio_read(tg3_phy_addr, MII_ADVERTISE, &local) ||
        !tg3_mdio_read(tg3_phy_addr, MII_CTRL1000, &local_gigabit)) {
        tg3_link_up = 0;
        return;
    }
    uint32_t mode = tg3_read32(TG3_MAC_MODE) & ~0xEu;
    uint32_t common = partner & local;
    uint32_t gigabit_common = (gigabit >> 2) & local_gigabit;
    if (gigabit_common & 0x200) mode |= TG3_MAC_MODE_PORT_GMII;
    else if (gigabit_common & 0x100) mode |= TG3_MAC_MODE_PORT_GMII | 2;
    else {
        mode |= 4; /* MII for 10/100 */
        if (common & 0x100) { /* 100 full duplex */ }
        else if (common & 0x80) mode |= 2;
        else if (common & 0x40) { /* 10 full duplex */ }
        else if (common & 0x20) mode |= 2;
        else { tg3_link_up = 0; return; }
    }
    tg3_write32(TG3_MAC_MODE, mode);
    control = tg3_read32(TG3_MAC_TX_LENGTHS) & ~0xFFu;
    /* Linux uses the long slot only for 1000BASE-T half duplex. */
    uint8_t long_slot = (mode & 0xE) == (TG3_MAC_MODE_PORT_GMII | 2);
    tg3_write32(TG3_MAC_TX_LENGTHS, control | (long_slot ? 0xFF : 0x20));
    tg3_link_up = 1;
}

uint8_t tg3_is_ready(void) {
    if (tg3_ready) tg3_update_link();
    return tg3_ready && tg3_link_up;
}

void tg3_shutdown(void) {
    if (!tg3_mmio_base) return;
    tg3_write32(TG3_MAC_RX_MODE, 0);
    tg3_write32(TG3_MAC_TX_MODE, 0);
    static const uint16_t modes[] = {
        TG3_SNDDATAI_MODE, TG3_SNDDATAC_MODE, TG3_SNDBDS_MODE,
        TG3_SNDBDI_MODE, TG3_SNDBDC_MODE, TG3_RCVLPC_MODE,
        TG3_RCVDBDI_MODE, TG3_RCVDCC_MODE, TG3_RCVBDI_MODE,
        TG3_RCVCC_MODE, TG3_HOSTCC_MODE, TG3_RDMAC_MODE, TG3_WDMAC_MODE
    };
    for (uint32_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        tg3_write32(modes[i], 0);
        uint32_t wait;
        for (wait = 0; wait < 10000; wait++) {
            if (!(tg3_read32(modes[i]) & TG3_BLOCK_ENABLE)) break;
            driver_delay_us(10);
        }
        if (wait == 10000) {
            tg3_diag.init_failure = NET_INIT_FAILURE_TIMEOUT;
            tg3_diag.last_error_status = modes[i];
        }
    }
    if (tg3_pci) pci_set_master(tg3_pci, 0);
    tg3_link_up = 0;
    tg3_ready = 0;
}

static void tg3_reap_tx(void) {
    uint16_t consumer = status_block.tx_consumer;
    __asm__ volatile ("" ::: "memory");
    uint16_t completed = (consumer - tg3_tx_cons) & (TG3_TX_RING_SIZE - 1);
    uint16_t pending = (tg3_tx_prod - tg3_tx_cons) & (TG3_TX_RING_SIZE - 1);
    if (consumer >= TG3_TX_RING_SIZE || completed > pending) return;
    tg3_diag.tx_completed += completed;
    tg3_tx_cons = consumer;
}

uint8_t tg3_send(const uint8_t* data, uint32_t length) {
    tg3_diag.tx_attempts++;
    if (!tg3_ready || !data || !length || length > NET_MAX_FRAME_SIZE) return 0;
    tg3_reap_tx();
    uint16_t pending = (tg3_tx_prod - tg3_tx_cons) & (TG3_TX_RING_SIZE - 1);
    if (pending >= TG3_TX_BUFFER_COUNT - 1) {
        tg3_diag.tx_busy++;
        return 0;
    }
    uint8_t* dst = tx_buffers[tg3_tx_prod % TG3_TX_BUFFER_COUNT];
    for (uint32_t i = 0; i < length; i++) dst[i] = data[i];
    /* Pad short Ethernet frames without reading beyond the caller's buffer. */
    while (length < 60) dst[length++] = 0;
    tx_ring[tg3_tx_prod].addr_hi = 0;
    tx_ring[tg3_tx_prod].addr_lo = (uint32_t)dst;
    tx_ring[tg3_tx_prod].vlan_tag = 0;
    tx_ring[tg3_tx_prod].len_flags = (length << 16) | TG3_TXD_END;
    __asm__ volatile ("" ::: "memory");
    tg3_tx_prod = (tg3_tx_prod + 1) & (TG3_TX_RING_SIZE - 1);
    tg3_write32(TG3_SND_HOST_PROD_IDX_MBOX, tg3_tx_prod);
    tg3_diag.tx_submitted++;
    return 1;
}

void tg3_poll(void) {
    if (!tg3_ready) return;
    tg3_reap_tx();
    if ((tg3_link_poll++ & 255u) == 0) tg3_update_link();
    /* Force a fresh DMA status snapshot even though PCI interrupts are masked. */
    tg3_write32(TG3_HOSTCC_MODE, TG3_HOSTCC_MODE_ENABLE | TG3_HOSTCC_MODE_NOW);
    uint16_t producer = status_block.rx_producer;
    __asm__ volatile ("" ::: "memory");
    if (producer >= TG3_RX_RING_SIZE) { tg3_diag.rx_errors++; return; }
    for (uint32_t budget = 8; budget && tg3_rx_rcb_cons != producer; budget--) {
        volatile tg3_rx_desc_t* desc = &rx_ret_ring[tg3_rx_rcb_cons];
        uint32_t len = desc->idx_len & 0xFFFF;
        uint32_t opaque = desc->opaque;
        uint32_t buf_idx = opaque & 0xFFFF;
        tg3_diag.last_rx_length = len;
        tg3_diag.rx_descriptor_status = desc->type_flags;
        if ((opaque & TG3_RXD_RING_MASK) != TG3_RXD_STD_RING || buf_idx >= TG3_BUFFER_COUNT) {
            tg3_diag.rx_invalid++;
        } else {
            if (desc->type_flags & TG3_RXD_ERROR) tg3_diag.rx_errors++;
            else if (len >= 18 && len <= TG3_BUF_SIZE) {
                tg3_diag.rx_frames++;
                if (tg3_rx_handler) tg3_rx_handler(rx_buffers[buf_idx], len - 4);
            } else tg3_diag.rx_invalid++;
            /* Repost in producer order, independently of the buffer's opaque ID. */
            tg3_rx_desc_t* next = &rx_std_ring[tg3_rx_std_prod];
            next->addr_hi = 0;
            next->addr_lo = (uint32_t)rx_buffers[buf_idx];
            next->idx_len = (tg3_rx_std_prod << 16) | TG3_BUF_SIZE;
            next->type_flags = TG3_RXD_END;
            next->opaque = TG3_RXD_STD_RING | buf_idx;
            __asm__ volatile ("" ::: "memory");
            tg3_rx_std_prod = (tg3_rx_std_prod + 1) & (TG3_RX_RING_SIZE - 1);
            tg3_write32(TG3_RCV_STD_PROD_IDX_MBOX, tg3_rx_std_prod);
        }
        tg3_rx_rcb_cons = (tg3_rx_rcb_cons + 1) & (TG3_RX_RING_SIZE - 1);
        tg3_write32(TG3_RCV_RET_CONS_IDX_MBOX, tg3_rx_rcb_cons);
    }
}

void tg3_set_receive_handler(net_receive_handler_t handler) {
    tg3_rx_handler = handler;
}

uint64_t tg3_get_mac_address(void) {
    return tg3_mac_addr;
}

void tg3_set_ip_address(uint32_t address) {
    tg3_ip_addr = address;
}

uint32_t tg3_get_ip_address(void) {
    return tg3_ip_addr;
}

void tg3_get_diagnostics(net_diagnostics_t* output) {
    if (!output) return;
    *output = tg3_diag;
    output->initialized = tg3_ready;
    output->io_base = (uint16_t)tg3_mmio_base;
    output->tx_index = (uint8_t)tg3_tx_prod;
    output->rx_index = (uint8_t)tg3_rx_rcb_cons;
    output->phy_addr = tg3_phy_addr;
    output->phy_id = tg3_phy_id;
}

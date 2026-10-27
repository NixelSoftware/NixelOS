#include "net_tg3.h"
#include "serial.h"

#define PCI_CONFIG_ADDRESS          0xCF8
#define PCI_CONFIG_DATA             0xCFC
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
static net_receive_handler_t tg3_rx_handler = 0;
static net_diagnostics_t tg3_diag;

static uint16_t tg3_tx_prod = 0;
static uint16_t tg3_rx_rcb_cons = 0;
static uint16_t tg3_rx_std_prod, tg3_tx_cons;
static uint16_t tg3_device;
static uint8_t tg3_link_up;
static uint32_t tg3_link_poll;

static tg3_rx_desc_t rx_std_ring[TG3_RX_RING_SIZE] __attribute__((aligned(64)));
static volatile tg3_rx_desc_t rx_ret_ring[TG3_RX_RING_SIZE] __attribute__((aligned(64)));
static tg3_tx_desc_t tx_ring[TG3_TX_RING_SIZE]     __attribute__((aligned(64)));
static volatile tg3_status_block_t status_block    __attribute__((aligned(64)));

/* Buffer pools are smaller than the hardware rings to fit kernel BSS. */
static uint8_t rx_buffers[TG3_BUFFER_COUNT][TG3_BUF_SIZE] __attribute__((aligned(64)));
static uint8_t tx_buffers[TG3_TX_BUFFER_COUNT][TG3_BUF_SIZE] __attribute__((aligned(64)));

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

static inline void tg3_delay(void) {
    for (volatile int d = 0; d < 3000; d++) {
        outb(0x80, 0x00);
    }
}

static void tg3_write_mem(uint32_t off, uint32_t val) {
    tg3_write32(TG3PCI_MEM_WIN_BASE_ADDR, off);
    tg3_write32(TG3PCI_MEM_WIN_DATA, val);
    tg3_write32(TG3PCI_MEM_WIN_BASE_ADDR, 0);
}

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
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t id = pci_read((uint8_t)bus, slot, func, 0);
                uint16_t vendor = (uint16_t)id;
                uint16_t device = (uint16_t)(id >> 16);

                if (vendor == BROADCOM_VENDOR_ID && is_tg3_device(device)) {
                    uint32_t bar0 = pci_read((uint8_t)bus, slot, func, 0x10);
                    /* Reject I/O, reserved BAR types, and addresses above 4 GiB. */
                    if (!bar0 || bar0 == 0xFFFFFFFF || (bar0 & 1) ||
                        (bar0 & 6) == 2 || (bar0 & 6) == 6 ||
                        ((bar0 & 6) == 4 && pci_read(bus, slot, func, 0x14)))
                        continue;
                    tg3_mmio_base = bar0 & ~0xFU;
                    if (!tg3_mmio_base) continue;
                    tg3_device = device;
                    uint32_t cmd = pci_read(bus, slot, func, 0x04) & 0xFFFF;
                    /* Do not echo PCI status W1C bits back into config space. */
                    pci_write32(bus, slot, func, 0x04, cmd | 0x406);
                    return 1;
                }
            }
        }
    }
    return 0;
}

static void tg3_read_mac(void) {
    uint32_t hi = tg3_read32(TG3_MAC_ADDR_0_HIGH);
    uint32_t lo = tg3_read32(TG3_MAC_ADDR_0_LOW);

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
        outb(0x80, 0x00);
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
        outb(0x80, 0x00);
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
        outb(0x80, 0x00);
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
        outb(0x80, 0x00);
    }

    if (orig_mi_mode & MAC_MI_MODE_AUTO_POLL) {
        tg3_write32(TG3_MAC_MI_MODE, orig_mi_mode);
        tg3_delay();
    }

    return ok;
}

static int tg3_phy_init(void) {
    uint32_t val = 0;

    tg3_write32(TG3_MAC_MI_MODE, MAC_MI_MODE_BASE | MAC_MI_MODE_CLK_10MHZ);
    tg3_delay();

    tg3_phy_addr = 1;
    int found = 0;
    for (uint8_t a = 0; a < 32; a++) {
        val = 0;
        if (tg3_mdio_read(a, MII_PHYID1, &val) && val != 0x0000 && val != 0xFFFF) {
            tg3_phy_addr = a;
            found = 1;
            break;
        }
    }

    if (!found) return 0;
    tg3_phy_id = (uint16_t)val;

    if (!tg3_mdio_write(tg3_phy_addr, MII_BMCR, BMCR_RESET)) return 0;
    int reset_done = 0;
    for (int i = 0; i < 1000; i++) {
        if (tg3_mdio_read(tg3_phy_addr, MII_BMCR, &val) && !(val & BMCR_RESET)) {
            reset_done = 1;
            break;
        }
        tg3_delay();
    }

    if (!reset_done || !tg3_mdio_read(tg3_phy_addr, MII_BMCR, &val)) return 0;
    val &= ~BMCR_PDOWN;
    if (!tg3_mdio_write(tg3_phy_addr, MII_BMCR, val)) return 0;

    if (!tg3_mdio_write(tg3_phy_addr, MII_ADVERTISE, ADVERTISE_CSMA | ADVERTISE_ALL) ||
        !tg3_mdio_write(tg3_phy_addr, MII_CTRL1000, ADVERTISE_1000FULL) ||
        !tg3_mdio_write(tg3_phy_addr, MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART))
        return 0;

    /* Poll copper link explicitly instead of racing firmware autopoll. */
    tg3_write32(TG3_MAC_MI_MODE, MAC_MI_MODE_BASE | MAC_MI_MODE_CLK_10MHZ);
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

    for (uint32_t i = 0; i < sizeof(tg3_diag); i++) {
        ((uint8_t*)&tg3_diag)[i] = 0;
    }

    if (!tg3_pci_find()) {
        tg3_diag.init_failure = NET_INIT_FAILURE_DISCOVERY;
        return 0;
    }

    debug_print("[TG3] Experimental PCIe copper driver, BAR: ");
    debug_hex(tg3_mmio_base);
    debug_print("\n");
    if (tg3_read32(TG3_PCI_MISC_HOST_CTRL) == 0xFFFFFFFF) {
        tg3_diag.init_failure = NET_INIT_FAILURE_REGISTER_ACCESS;
        return 0;
    }
    tg3_write32(TG3_PCI_MISC_HOST_CTRL, (tg3_read32(TG3_PCI_MISC_HOST_CTRL) & ~0x44u) | 0x88u | TG3_MISC_HOST_CTRL_MASK_INT);

    tg3_shutdown(); /* Quiesce a previous firmware/OS data path first. */
    if (tg3_diag.init_failure == NET_INIT_FAILURE_TIMEOUT) return 0;
    uint32_t misc_cfg = tg3_read32(TG3_GRC_MISC_CFG);
    misc_cfg &= ~GRC_MISC_CFG_EPHY_IDDQ;
    misc_cfg |= GRC_MISC_CFG_KEEP_GPHY_PWR;
    tg3_write32(TG3_GRC_MISC_CFG, misc_cfg);

    /* PCI memory-read (6) / memory-write (7) commands, plus PCIe watermark. */
    tg3_write32(TG3_DMA_RWCTRL, 0x76180000);

    uint32_t grc_mode = tg3_read32(TG3_GRC_MODE);
    /* Little-endian host descriptors, packet bytes in network order. */
    grc_mode &= ~(0x2u | 0x20000000u);
    grc_mode |= TG3_GRC_MODE_LE_DMA | TG3_GRC_MODE_STACK_UP | TG3_GRC_MODE_HOST_SENDBDS | TG3_GRC_MODE_NO_TX_PHDR_CSUM;
    tg3_write32(TG3_GRC_MODE, grc_mode);

    tg3_write32(TG3_HOSTCC_MODE, 0);
    tg3_write32(TG3_MEMARB_MODE, TG3_MEMARB_MODE_ENABLE);
    /* Standard-MTU watermarks for the selected PCIe families. */
    uint8_t newer = (tg3_device & 0xFFF0) == 0x16B0;
    tg3_write32(TG3_BUFMGR_RDMA_LOW_WATER, 0);
    tg3_write32(TG3_BUFMGR_MACRX_LOW_WATER, newer ? 0x2A : 0x10);
    tg3_write32(TG3_BUFMGR_HIGH_WATER, newer ? 0xA0 : 0x60);
    tg3_write32(TG3_BUFMGR_DMA_LOW_WATER, 5);
    tg3_write32(TG3_BUFMGR_DMA_HIGH_WATER, 10);
    tg3_write32(TG3_BUFMGR_MODE, TG3_BUFMGR_MODE_ENABLE);
    tg3_write32(TG3_FTQ_RESET, 0xFFFFFFFF);
    tg3_write32(TG3_FTQ_RESET, 0x00000000);

    tg3_read_mac();
    if (!tg3_mac_addr || tg3_mac_addr == 0xFFFFFFFFFFFFULL ||
        (tg3_mac_addr & 0x010000000000ULL)) {
        tg3_diag.init_failure = NET_INIT_FAILURE_REGISTER_ACCESS;
        tg3_shutdown();
        return 0;
    }

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
    uint32_t std_config = (tg3_device & 0xFFF0) == 0x16B0 ?
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
    tg3_write32(TG3_RDMAC_MODE, TG3_BLOCK_ENABLE); /* Read DMA */
    tg3_write32(TG3_RCVDCC_MODE, TG3_BLOCK_ENABLE); /* Receive data completion */
    tg3_write32(TG3_SNDDATAI_MODE, TG3_BLOCK_ENABLE); /* Send data initiator */
    tg3_write32(TG3_SNDDATAC_MODE, TG3_BLOCK_ENABLE); /* Send data completion */
    tg3_write32(TG3_SNDBDS_MODE, TG3_BLOCK_ENABLE); /* Send BD selector */
    tg3_write32(TG3_SNDBDI_MODE, TG3_BLOCK_ENABLE); /* Send BD initiator */
    tg3_write32(TG3_SNDBDC_MODE, TG3_BLOCK_ENABLE); /* Send BD completion */
    tg3_write32(TG3_WDMAC_MODE, WDMAC_MODE_ENABLE);
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
    tg3_write32(TG3_MAC_MODE, TG3_MAC_MODE_PORT_GMII | TG3_MAC_MODE_DMA_ENABLE);

    if (!tg3_phy_init()) {
        tg3_diag.init_failure = NET_INIT_FAILURE_TIMEOUT;
        /* Teardown must also work after partially completed initialization. */
        tg3_shutdown();
        return 0;
    }

    tg3_write32(0x00003C08, 1);
    tg3_write32(0x00003C10, 1);
    tg3_write32(0x00003C0C, 1);
    tg3_write32(0x00003C14, 1);
    tg3_write32(TG3_HOSTCC_MODE, TG3_HOSTCC_MODE_ENABLE);

    debug_print("[TG3] DMA rings configured; PHY: ");
    debug_hex(tg3_phy_addr);
    debug_print(" ID: ");
    debug_hex(tg3_phy_id);
    debug_print("\n");
    tg3_ready = 1;
    tg3_diag.initialized = 1;
    return 1;
}

/* MAC mode follows negotiated copper speed/duplex, not the link-change bit. */
static void tg3_update_link(void) {
    uint32_t status, partner, gigabit, control;
    if (!tg3_mdio_read(tg3_phy_addr, MII_BMSR, &status) ||
        !tg3_mdio_read(tg3_phy_addr, MII_BMSR, &status) ||
        !(status & 4) || !(status & 0x20)) {
        tg3_link_up = 0;
        return;
    }
    if (!tg3_mdio_read(tg3_phy_addr, 10, &gigabit) ||
        !tg3_mdio_read(tg3_phy_addr, 5, &partner)) {
        tg3_link_up = 0;
        return;
    }
    uint32_t mode = tg3_read32(TG3_MAC_MODE) & ~0xEu;
    if (gigabit & 0x800) mode |= TG3_MAC_MODE_PORT_GMII;
    else {
        mode |= 4; /* MII for 10/100 */
        if (!(partner & 0x100) && ((partner & 0x80) || !(partner & 0x40))) mode |= 2;
    }
    tg3_write32(TG3_MAC_MODE, mode);
    control = tg3_read32(TG3_MAC_TX_LENGTHS) & ~0xFFu;
    tg3_write32(TG3_MAC_TX_LENGTHS, control | ((mode & 2) ? 0xFF : 0x20));
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
            outb(0x80, 0);
        }
        if (wait == 10000) {
            tg3_diag.init_failure = NET_INIT_FAILURE_TIMEOUT;
            tg3_diag.last_error_status = modes[i];
        }
    }
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

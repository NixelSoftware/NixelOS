/* Realtek RTL8169/8110/8168/810x/8111 PCI Ethernet, polled DMA driver.
 *
 * Register level port of Linux drivers/net/ethernet/realtek/r8169_main.c
 * (GPL-2.0-only).  No kernel device model, workqueues, phylib or firmware
 * loader: Nixel drives the MAC directly from the boot-time poll loop.
 *
 * The legacy 16-byte descriptor format is used (one TX queue, one RX queue),
 * which is what RTL8169 through RTL8111 provide.  RTL8125 and newer use the
 * same registers but add RSS/multi-queue descriptor variants and firmware;
 * they are deliberately rejected rather than driven half-initialised.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "r8169.h"
#include "../../drivers/delay.h"
#include "../../drivers/pci.h"
#include "../../drivers/serial.h"

#define RTL_VENDOR_ID 0x10EC

/* ---- Register offsets (r8169_main.c enum rtl_registers) ---------------- */
#define MAC0                 0x00
#define MAC4                 0x04
#define MAR0                 0x08
#define TxDescStartAddrLow   0x20
#define TxDescStartAddrHigh  0x24
#define ChipCmd              0x37
#define TxPoll               0x38
#define IntrMask             0x3c
#define IntrStatus           0x3e
#define TxConfig             0x40
#define RxConfig             0x44
#define Cfg9346              0x50
#define Config1              0x52
#define Config2              0x53
#define Config3              0x54
#define Config4              0x55
#define Config5              0x56
#define PHYAR                0x60
#define ERIDR                0x70
#define ERIAR                0x74
#define GPHY_OCP             0xb8
#define DLLPR                0xd0
#define RxMaxSize            0xda
#define CPlusCmd             0xe0
#define IntrMitigate         0xe2
#define RxDescAddrLow        0xe4
#define RxDescAddrHigh       0xe8
#define EarlyTxThres         0xec

/* ---- ChipCmd bits ------------------------------------------------------ */
#define CmdReset   0x10
#define CmdRxEnb   0x08
#define CmdTxEnb   0x04

/* ---- Interrupt/status bits -------------------------------------------- */
#define IntrErrBits (0x8000 | 0x0040 | 0x0010 | 0x0008 | 0x0002)

/* ---- Cfg9346 ----------------------------------------------------------- */
#define Cfg9346_Lock   0x00
#define Cfg9346_Unlock 0xc0

/* ---- Config1/2/3/5 bits ------------------------------------------------ */
#define Config1_LEDS1      0x80
#define Config1_LEDS0      0x40
#define Config1_Speed_down 0x10
#define Config1_PMEnable   0x01
#define Config2_ClkReqEn   0x80
#define Config2_MSIEnable  0x20
#define Config3_MagicPkt   0x20
#define Config3_LinkUp     0x10
#define Config4_Jumbo_En1  0x02
#define Config5_BWF        0x40
#define Config5_MWF        0x20
#define Config5_UWF        0x10
#define Config5_LanWake    0x02
#define Config5_PMEStatus  0x01
#define Config5_ASPM_en    0x01

/* ---- CPlusCmd ---------------------------------------------------------- */
#define CPlusCmd_PCIMulRW 0x0008
#define CPlusCmd_MASK     0x2063

/* ---- TxConfig / RxConfig ---------------------------------------------- */
#define TXCFG_AUTO_FIFO (1 << 7)
#define TX_DMA_BURST    (7 << 8)
#define TX_IFG          (3 << 24)
#define RX128_INT_EN    (1 << 15)
#define RX_MULTI_EN     (1 << 14)
#define RX_FIFO_THRESH  (7 << 13)
#define RX_EARLY_OFF    (1 << 11)
#define RX_DMA_BURST    (7 << 8)

/* ---- RxConfig accept mask --------------------------------------------- */
#define AcceptErr       0x20
#define AcceptRunt      0x10
#define AcceptBroadcast 0x08
#define AcceptMulticast 0x04
#define AcceptMyPhys    0x02
#define AcceptAllPhys   0x01
#define RX_ACCEPT_MASK  0x3f

/* ---- ERI / PHY / OCP access ------------------------------------------- */
#define ERIAR_FLAG          0x80000000u
#define ERIAR_WRITE_CMD     0x80000000u
#define ERIAR_READ_CMD      0x00000000u
#define ERIAR_MASK_1111     (0xf << 12)
#define ERIAR_EXGMAC        (0x00 << 16)

#define PHYAR_FLAG          0x80000000u
#define OCPAR_FLAG          0x80000000u
#define OCP_STD_PHY_BASE    0xa400u
#define DP2_MDIO_ACCESS_BIT 0x00020000u

/* ---- Descriptor bits ---------------------------------------------------- */
#define DescOwn   (1u << 31)
#define RingEnd   (1u << 30)
#define FirstFrag (1u << 29)
#define LastFrag  (1u << 28)

/* Rx private status bits. */
#define RxRWT (1u << 22)
#define RxRES (1u << 21)
#define RxRUNT (1u << 20)
#define RxCRC (1u << 19)

#define TX_POLL_NPQ 0x40
#define TX_MAX_EARLY 0x3f        /* NoEarlyTx */
#define TX_PACKET_MAX (8064 >> 7)
#define TX_EARLY_SIZE 0x27

/* ---- Ring geometry ------------------------------------------------------ */
#define NUM_TX_DESC 32
#define NUM_RX_DESC 32
#define RX_BUFFER_SIZE 2048
#define RX_POLL_BUDGET 16

/* ---- MAC versions, ordered exactly like Linux enum mac_version ---------- */
enum {
    RTL_GIGA_MAC_VER_02 = 0, RTL_GIGA_MAC_VER_03, RTL_GIGA_MAC_VER_04,
    RTL_GIGA_MAC_VER_05, RTL_GIGA_MAC_VER_06, RTL_GIGA_MAC_VER_07,
    RTL_GIGA_MAC_VER_08, RTL_GIGA_MAC_VER_09, RTL_GIGA_MAC_VER_10,
    RTL_GIGA_MAC_VER_14, RTL_GIGA_MAC_VER_17, RTL_GIGA_MAC_VER_18,
    RTL_GIGA_MAC_VER_19, RTL_GIGA_MAC_VER_20, RTL_GIGA_MAC_VER_21,
    RTL_GIGA_MAC_VER_22, RTL_GIGA_MAC_VER_23, RTL_GIGA_MAC_VER_24,
    RTL_GIGA_MAC_VER_25, RTL_GIGA_MAC_VER_26, RTL_GIGA_MAC_VER_28,
    RTL_GIGA_MAC_VER_29, RTL_GIGA_MAC_VER_30, RTL_GIGA_MAC_VER_31,
    RTL_GIGA_MAC_VER_32, RTL_GIGA_MAC_VER_33, RTL_GIGA_MAC_VER_34,
    RTL_GIGA_MAC_VER_35, RTL_GIGA_MAC_VER_36, RTL_GIGA_MAC_VER_37,
    RTL_GIGA_MAC_VER_38, RTL_GIGA_MAC_VER_39, RTL_GIGA_MAC_VER_40,
    RTL_GIGA_MAC_VER_42, RTL_GIGA_MAC_VER_43, RTL_GIGA_MAC_VER_44,
    RTL_GIGA_MAC_VER_46, RTL_GIGA_MAC_VER_48, RTL_GIGA_MAC_VER_51,
    RTL_GIGA_MAC_VER_52, RTL_GIGA_MAC_VER_61, RTL_GIGA_MAC_VER_63,
    RTL_GIGA_MAC_VER_64, RTL_GIGA_MAC_VER_65, RTL_GIGA_MAC_VER_66,
    RTL_GIGA_MAC_VER_70, RTL_GIGA_MAC_VER_80, RTL_GIGA_MAC_NONE
};

typedef struct {
    uint32_t mask;
    uint32_t value;
    uint8_t  version;
    const char* name;
} r8169_chip_t;

static const r8169_chip_t r8169_chips[] = {
    { 0x7cf, 0x6c9, RTL_GIGA_MAC_VER_80, "RTL8127A" },
    { 0x7cf, 0x64a, RTL_GIGA_MAC_VER_70, "RTL8126A" },
    { 0x7cf, 0x649, RTL_GIGA_MAC_VER_70, "RTL8126A" },
    { 0x7cf, 0x681, RTL_GIGA_MAC_VER_66, "RTL8125BP" },
    { 0x7cf, 0x708, RTL_GIGA_MAC_VER_65, "RTL8125CP" },
    { 0x7cf, 0x68b, RTL_GIGA_MAC_VER_64, "RTL9151A" },
    { 0x7cf, 0x68a, RTL_GIGA_MAC_VER_64, "RTL8125K" },
    { 0x7cf, 0x689, RTL_GIGA_MAC_VER_64, "RTL8125D" },
    { 0x7cf, 0x688, RTL_GIGA_MAC_VER_64, "RTL8125D" },
    { 0x7cf, 0x641, RTL_GIGA_MAC_VER_63, "RTL8125B" },
    { 0x7cf, 0x609, RTL_GIGA_MAC_VER_61, "RTL8125A" },
    { 0x7cf, 0x54b, RTL_GIGA_MAC_VER_52, "RTL8168fp/RTL8117" },
    { 0x7cf, 0x54a, RTL_GIGA_MAC_VER_52, "RTL8168fp/RTL8117" },
    { 0x7cf, 0x502, RTL_GIGA_MAC_VER_51, "RTL8168ep/8111ep" },
    { 0x7cf, 0x541, RTL_GIGA_MAC_VER_46, "RTL8168h/8111h" },
    { 0x7cf, 0x6c0, RTL_GIGA_MAC_VER_46, "RTL8168M" },
    { 0x7cf, 0x5c8, RTL_GIGA_MAC_VER_44, "RTL8411b" },
    { 0x7cf, 0x509, RTL_GIGA_MAC_VER_42, "RTL8168gu/8111gu" },
    { 0x7cf, 0x4c0, RTL_GIGA_MAC_VER_40, "RTL8168g/8111g" },
    { 0x7c8, 0x488, RTL_GIGA_MAC_VER_38, "RTL8411" },
    { 0x7cf, 0x481, RTL_GIGA_MAC_VER_36, "RTL8168f/8111f" },
    { 0x7cf, 0x480, RTL_GIGA_MAC_VER_35, "RTL8168f/8111f" },
    { 0x7c8, 0x2c8, RTL_GIGA_MAC_VER_34, "RTL8168evl/8111evl" },
    { 0x7cf, 0x2c1, RTL_GIGA_MAC_VER_32, "RTL8168e/8111e" },
    { 0x7c8, 0x2c0, RTL_GIGA_MAC_VER_33, "RTL8168e/8111e" },
    { 0x7cf, 0x281, RTL_GIGA_MAC_VER_25, "RTL8168d/8111d" },
    { 0x7c8, 0x280, RTL_GIGA_MAC_VER_26, "RTL8168d/8111d" },
    { 0x7cf, 0x28a, RTL_GIGA_MAC_VER_28, "RTL8168dp/8111dp" },
    { 0x7cf, 0x28b, RTL_GIGA_MAC_VER_31, "RTL8168dp/8111dp" },
    { 0x7cf, 0x3c9, RTL_GIGA_MAC_VER_23, "RTL8168cp/8111cp" },
    { 0x7cf, 0x3c8, RTL_GIGA_MAC_VER_18, "RTL8168cp/8111cp" },
    { 0x7c8, 0x3c8, RTL_GIGA_MAC_VER_24, "RTL8168cp/8111cp" },
    { 0x7cf, 0x3c0, RTL_GIGA_MAC_VER_19, "RTL8168c/8111c" },
    { 0x7cf, 0x3c2, RTL_GIGA_MAC_VER_20, "RTL8168c/8111c" },
    { 0x7cf, 0x3c3, RTL_GIGA_MAC_VER_21, "RTL8168c/8111c" },
    { 0x7c8, 0x3c0, RTL_GIGA_MAC_VER_22, "RTL8168c/8111c" },
    { 0x7c8, 0x380, RTL_GIGA_MAC_VER_17, "RTL8168b/8111b" },
    { 0x7c8, 0x448, RTL_GIGA_MAC_VER_39, "RTL8106e" },
    { 0x7c8, 0x440, RTL_GIGA_MAC_VER_37, "RTL8402" },
    { 0x7cf, 0x409, RTL_GIGA_MAC_VER_29, "RTL8105e" },
    { 0x7c8, 0x408, RTL_GIGA_MAC_VER_30, "RTL8105e" },
    { 0x7cf, 0x349, RTL_GIGA_MAC_VER_08, "RTL8102e" },
    { 0x7cf, 0x249, RTL_GIGA_MAC_VER_08, "RTL8102e" },
    { 0x7cf, 0x348, RTL_GIGA_MAC_VER_07, "RTL8102e" },
    { 0x7cf, 0x248, RTL_GIGA_MAC_VER_07, "RTL8102e" },
    { 0x7cf, 0x240, RTL_GIGA_MAC_VER_14, "RTL8401" },
    { 0x7c8, 0x348, RTL_GIGA_MAC_VER_09, "RTL8102e/RTL8103e" },
    { 0x7c8, 0x248, RTL_GIGA_MAC_VER_09, "RTL8102e/RTL8103e" },
    { 0x7c8, 0x340, RTL_GIGA_MAC_VER_10, "RTL8101e/RTL8100e" },
    { 0xfc8, 0x980, RTL_GIGA_MAC_VER_06, "RTL8169sc/8110sc" },
    { 0xfc8, 0x180, RTL_GIGA_MAC_VER_05, "RTL8169sc/8110sc" },
    { 0xfc8, 0x100, RTL_GIGA_MAC_VER_04, "RTL8169sb/8110sb" },
    { 0xfc8, 0x040, RTL_GIGA_MAC_VER_03, "RTL8110s" },
    { 0xfc8, 0x008, RTL_GIGA_MAC_VER_02, "RTL8169s" },
    { 0x000, 0x000, RTL_GIGA_MAC_NONE, 0 }
};

typedef struct {
    uint16_t vendor;
    uint16_t device;
} r8169_pci_id_t;

/* Realtek IDs handled by the legacy single-queue MAC.  RTL8125/8126/8127
 * are intentionally absent (see the file header). */
static const r8169_pci_id_t r8169_pci_ids[] = {
    { RTL_VENDOR_ID, 0x8169 }, { RTL_VENDOR_ID, 0x8168 },
    { RTL_VENDOR_ID, 0x8167 }, { RTL_VENDOR_ID, 0x8162 },
    { RTL_VENDOR_ID, 0x8161 }, { RTL_VENDOR_ID, 0x8136 },
    { RTL_VENDOR_ID, 0x8129 }, { RTL_VENDOR_ID, 0x2502 },
    { RTL_VENDOR_ID, 0x2600 }, { RTL_VENDOR_ID, 0x0e10 },
    { 0x1186, 0x4300 },        /* D-Link DGE-528T */
    { 0x1186, 0x4302 },
    { 0, 0 }
};

/* 16-byte hardware descriptor: opts1, opts2, 64-bit buffer address. */
typedef struct {
    volatile uint32_t opts1;
    volatile uint32_t opts2;
    volatile uint32_t addr_lo;
    volatile uint32_t addr_hi;
} r8169_desc_t;

static r8169_desc_t tx_ring[NUM_TX_DESC] __attribute__((aligned(256)));
static r8169_desc_t rx_ring[NUM_RX_DESC] __attribute__((aligned(256)));
static uint8_t tx_buffers[NUM_TX_DESC][RX_BUFFER_SIZE] __attribute__((aligned(16)));
static uint8_t rx_buffers[NUM_RX_DESC][RX_BUFFER_SIZE] __attribute__((aligned(16)));

static volatile uint8_t* mmio;
static const pci_device_t* pci_device;
static uint8_t ready, link_up, mac_version, phy_addr;
static uint64_t mac_address;
static uint32_t ip_address;
static uint16_t tx_cur, tx_dirty, rx_cur;
static uint32_t link_poll;
static net_receive_handler_t rx_handler;
static net_diagnostics_t diag;
static void (*init_progress)(const char* status);

static void progress(const char* status) {
    if (init_progress) init_progress(status);
    debug_print(status);
    debug_print("\n");
}

/* ---- MMIO access ------------------------------------------------------- */
static inline uint8_t read8(uint16_t reg) {
    return *(volatile uint8_t*)(mmio + reg);
}
static inline uint16_t read16(uint16_t reg) {
    return *(volatile uint16_t*)(mmio + reg);
}
static inline uint32_t read32(uint16_t reg) {
    return *(volatile uint32_t*)(mmio + reg);
}
static inline void write8(uint16_t reg, uint8_t value) {
    *(volatile uint8_t*)(mmio + reg) = value;
    (void)read8(reg);           /* drain posted PCI writes */
}
static inline void write16(uint16_t reg, uint16_t value) {
    *(volatile uint16_t*)(mmio + reg) = value;
    (void)read16(reg);
}
static inline void write32(uint16_t reg, uint32_t value) {
    *(volatile uint32_t*)(mmio + reg) = value;
    (void)read32(reg);
}

/* ---- PHY/MDIO ---------------------------------------------------------- */
static uint8_t is_dp_2(void) {
    return mac_version == RTL_GIGA_MAC_VER_28 ||
           mac_version == RTL_GIGA_MAC_VER_31;
}
static uint8_t is_ocp_phy(void) {
    return mac_version >= RTL_GIGA_MAC_VER_40 &&
           mac_version <= RTL_GIGA_MAC_VER_52;
}
static uint8_t is_8168evl_up(void) {
    return mac_version >= RTL_GIGA_MAC_VER_34 &&
           mac_version != RTL_GIGA_MAC_VER_37 &&
           mac_version != RTL_GIGA_MAC_VER_39;
}
static uint8_t supports_gmii(void) {
    switch (mac_version) {
    case RTL_GIGA_MAC_VER_07:
    case RTL_GIGA_MAC_VER_08:
    case RTL_GIGA_MAC_VER_09:
    case RTL_GIGA_MAC_VER_10:
    case RTL_GIGA_MAC_VER_14:
    case RTL_GIGA_MAC_VER_29:
    case RTL_GIGA_MAC_VER_30:
    case RTL_GIGA_MAC_VER_37:
    case RTL_GIGA_MAC_VER_39:
        return 0;
    default:
        return 1;
    }
}

static uint8_t phyar_write(uint8_t reg, uint16_t value) {
    write32(PHYAR, PHYAR_FLAG | ((uint32_t)(reg & 0x1f) << 16) | value);
    for (uint32_t i = 0; i < 20; ++i) {
        if (!(read32(PHYAR) & PHYAR_FLAG)) {
            driver_delay_us(20);
            return 1;
        }
        driver_delay_us(25);
    }
    return 0;
}
static uint8_t phyar_read(uint8_t reg, uint16_t* value) {
    write32(PHYAR, (uint32_t)(reg & 0x1f) << 16);
    for (uint32_t i = 0; i < 20; ++i) {
        uint32_t v = read32(PHYAR);
        if (v & PHYAR_FLAG) {
            if (value) *value = (uint16_t)v;
            driver_delay_us(20);
            return 1;
        }
        driver_delay_us(25);
    }
    return 0;
}
static uint8_t ocp_write(uint32_t reg, uint16_t value) {
    write32(GPHY_OCP, OCPAR_FLAG | ((reg & 0xffff) << 15) | value);
    for (uint32_t i = 0; i < 10; ++i) {
        if (!(read32(GPHY_OCP) & OCPAR_FLAG)) {
            driver_delay_us(20);
            return 1;
        }
        driver_delay_us(25);
    }
    return 0;
}
static uint8_t ocp_read(uint32_t reg, uint16_t* value) {
    write32(GPHY_OCP, (reg & 0xffff) << 15);
    for (uint32_t i = 0; i < 10; ++i) {
        uint32_t v = read32(GPHY_OCP);
        if (v & OCPAR_FLAG) {
            if (value) *value = (uint16_t)v;
            driver_delay_us(20);
            return 1;
        }
        driver_delay_us(25);
    }
    return 0;
}
static uint8_t phy_write(uint8_t reg, uint16_t value) {
    if (is_dp_2()) {
        write32(DLLPR, read32(DLLPR) & ~DP2_MDIO_ACCESS_BIT);
        uint8_t ok = phyar_write(reg, value);
        write32(DLLPR, read32(DLLPR) | DP2_MDIO_ACCESS_BIT);
        return ok;
    }
    if (is_ocp_phy()) return ocp_write(OCP_STD_PHY_BASE + (uint32_t)reg * 2, value);
    return phyar_write(reg, value);
}
static uint8_t phy_read(uint8_t reg, uint16_t* value) {
    if (is_dp_2()) {
        if (reg == 3) {           /* chip reports a bogus PHY ID */
            if (value) *value = 0xc912;
            return 1;
        }
        write32(DLLPR, read32(DLLPR) & ~DP2_MDIO_ACCESS_BIT);
        uint8_t ok = phyar_read(reg, value);
        write32(DLLPR, read32(DLLPR) | DP2_MDIO_ACCESS_BIT);
        return ok;
    }
    if (is_ocp_phy()) return ocp_read(OCP_STD_PHY_BASE + (uint32_t)reg * 2, value);
    return phyar_read(reg, value);
}

/* ---- ERI access (8168evl and newer MAC address in 0xe0/0xe4) ----------- */
static uint32_t eri_read(uint32_t address) {
    write32(ERIAR, ERIAR_READ_CMD | ERIAR_EXGMAC | ERIAR_MASK_1111 |
                    (address & ~3u));
    for (uint32_t i = 0; i < 100; ++i) {
        if (read32(ERIAR) & ERIAR_FLAG) return read32(ERIDR);
        driver_delay_us(100);
    }
    return 0xFFFFFFFFu;
}

/* ---- Reset / configuration -------------------------------------------- */
static uint8_t hw_reset(void) {
    write8(ChipCmd, CmdReset);
    for (uint32_t i = 0; i < 100; ++i) {
        if (!(read8(ChipCmd) & CmdReset)) return 1;
        driver_delay_us(100);
    }
    return 0;
}

static void unlock_config(void) { write8(Cfg9346, Cfg9346_Unlock); }
static void lock_config(void)   { write8(Cfg9346, Cfg9346_Lock); }

static void irq_mask_and_ack(void) {
    write16(IntrMask, 0);
    write16(IntrStatus, 0xFFFF);
}

/* ---- MAC address ------------------------------------------------------- */
static uint8_t mac_is_valid(const uint8_t* mac) {
    uint8_t any = 0, none = 0xFF;
    for (uint8_t i = 0; i < 6; ++i) {
        any |= mac[i];
        none &= mac[i];
    }
    return any != 0 && none != 0xFF && !(mac[0] & 1);
}

static void read_mac(uint8_t mac[6]) {
    if (is_8168evl_up() && mac_version != RTL_GIGA_MAC_VER_34) {
        uint32_t lo = eri_read(0xe0);
        uint32_t hi = eri_read(0xe4);
        mac[0] = (uint8_t)lo;
        mac[1] = (uint8_t)(lo >> 8);
        mac[2] = (uint8_t)(lo >> 16);
        mac[3] = (uint8_t)(lo >> 24);
        mac[4] = (uint8_t)hi;
        mac[5] = (uint8_t)(hi >> 8);
        if (mac_is_valid(mac)) return;
    }
    for (uint8_t i = 0; i < 6; ++i) mac[i] = read8((uint16_t)(MAC0 + i));
}

static void write_mac(const uint8_t mac[6]) {
    unlock_config();
    write16(MAC4, (uint16_t)(mac[4] | ((uint16_t)mac[5] << 8)));
    (void)read32(MAC0);                 /* PCI commit */
    write32(MAC0, (uint32_t)mac[0] | ((uint32_t)mac[1] << 8) |
                  ((uint32_t)mac[2] << 16) | ((uint32_t)mac[3] << 24));
    (void)read32(MAC0);
    lock_config();
}

/* ---- Chip discovery ---------------------------------------------------- */
static const r8169_chip_t* find_chip(uint32_t xid) {
    const r8169_chip_t* p = r8169_chips;
    while ((xid & p->mask) != p->value) ++p;
    return p;
}

static uint8_t device_supported(uint16_t vendor, uint16_t device) {
    for (uint32_t i = 0; r8169_pci_ids[i].vendor; ++i)
        if (r8169_pci_ids[i].vendor == vendor &&
            r8169_pci_ids[i].device == device) return 1;
    return 0;
}

static uint8_t discover(void) {
    for (uint32_t index = 0; index < pci_device_count(); ++index) {
        const pci_device_t* dev = pci_device_at(index);
        if (!device_supported(dev->vendor_id, dev->device_id)) continue;

        pci_device = dev;
        if (!pci_power_on(dev)) return 0;
        if (!pci_enable_device(dev, PCI_COMMAND_MEMORY)) return 0;
        pci_set_master(dev, 1);
        pci_prepare_polling(dev);

        /* First memory BAR.  Classic RTL8169 puts MMIO in BAR2; RTL8168
         * PCIe parts frequently expose it in BAR0. */
        mmio = 0;
        for (uint8_t bar_index = 0; bar_index < 6; ++bar_index) {
            pci_bar_t bar;
            if (!pci_get_bar(dev, bar_index, &bar)) continue;
            if (bar.io || !bar.address || bar.address > 0xFFFFFFFFull) continue;
            mmio = (volatile uint8_t*)(uint32_t)bar.address;
            break;
        }
        if (!mmio) return 0;
        return 1;
    }
    return 0;
}

/* ---- Hardware start ---------------------------------------------------- */
static void set_early_tx_or_packet_size(void) {
    if (mac_version <= RTL_GIGA_MAC_VER_06)
        write8(EarlyTxThres, TX_MAX_EARLY);
    else if (is_8168evl_up())
        write8(EarlyTxThres, TX_EARLY_SIZE);   /* MaxTxPacketSize */
    else
        write8(EarlyTxThres, TX_PACKET_MAX);
}

static void init_rxcfg(void) {
    uint32_t value;
    if (mac_version <= RTL_GIGA_MAC_VER_17)
        value = RX_FIFO_THRESH | RX_DMA_BURST;
    else if ((mac_version >= RTL_GIGA_MAC_VER_18 &&
              mac_version <= RTL_GIGA_MAC_VER_24) ||
             (mac_version >= RTL_GIGA_MAC_VER_34 &&
              mac_version <= RTL_GIGA_MAC_VER_36) ||
             mac_version == RTL_GIGA_MAC_VER_38)
        value = RX128_INT_EN | RX_MULTI_EN | RX_DMA_BURST;
    else if (mac_version >= RTL_GIGA_MAC_VER_40 &&
             mac_version <= RTL_GIGA_MAC_VER_52)
        value = RX128_INT_EN | RX_MULTI_EN | RX_DMA_BURST | RX_EARLY_OFF;
    else
        value = RX128_INT_EN | RX_DMA_BURST;
    write32(RxConfig, value);
}

static void set_rx_mode(void) {
    write32(MAR0 + 4, 0xFFFFFFFFu);
    write32(MAR0, 0xFFFFFFFFu);
    write32(RxConfig, (read32(RxConfig) & ~RX_ACCEPT_MASK) |
                      AcceptBroadcast | AcceptMulticast | AcceptMyPhys);
}

static void init_rings(void) {
    tx_cur = tx_dirty = 0;
    rx_cur = 0;

    for (uint16_t i = 0; i < NUM_TX_DESC; ++i) {
        tx_ring[i].opts1 = 0;
        tx_ring[i].opts2 = 0;
        tx_ring[i].addr_lo = (uint32_t)tx_buffers[i];
        tx_ring[i].addr_hi = 0;
    }
    for (uint16_t i = 0; i < NUM_RX_DESC; ++i) {
        rx_ring[i].opts2 = 0;
        rx_ring[i].addr_lo = (uint32_t)rx_buffers[i];
        rx_ring[i].addr_hi = 0;
        rx_ring[i].opts1 = DescOwn | RX_BUFFER_SIZE |
                           (i == NUM_RX_DESC - 1 ? RingEnd : 0);
    }
    __asm__ volatile ("" ::: "memory");

    write32(TxDescStartAddrLow, (uint32_t)&tx_ring[0]);
    write32(TxDescStartAddrHigh, 0);
    write32(RxDescAddrLow, (uint32_t)&rx_ring[0]);
    write32(RxDescAddrHigh, 0);
}

static void init_phy(void) {
    uint16_t id1 = 0, id2 = 0;
    phy_addr = 0;

    if (!phy_read(2, &id1) || !phy_read(3, &id2)) return;
    diag.phy_addr = phy_addr;
    diag.phy_id = id1;

    /* Soft reset. */
    (void)phy_write(0, 0x8000);
    for (uint32_t i = 0; i < 1000; ++i) {
        uint16_t bmcr = 0;
        if (!phy_read(0, &bmcr) || !(bmcr & 0x8000)) break;
        driver_delay_us(100);
    }

    /* Advertise 10/100 and, where the PHY has it, 1000BASE-T. */
    (void)phy_write(4, 0x01E1);
    if (supports_gmii()) (void)phy_write(9, 0x0300);

    /* Auto-negotiation enable + restart. */
    (void)phy_write(0, 0x1200);
}

static void update_link(void) {
    uint16_t bmsr = 0;
    /* Link status is latched low on several Realtek PHYs: read twice. */
    if (!phy_read(1, &bmsr) || !phy_read(1, &bmsr)) {
        link_up = 0;
        return;
    }
    link_up = (bmsr & 0x0004) ? 1 : 0;
}

static uint8_t fail(uint8_t reason) {
    debug_print("[R8169] Initialization failed: ");
    debug_hex(reason);
    debug_print("\n");
    progress("Realtek: initialization failed, stopping");
    diag.init_failure = reason;
    r8169_shutdown();
    return 0;
}

uint8_t r8169_init(void) {
    progress("Realtek: entering driver");
    if (mmio) r8169_shutdown();
    mmio = 0;
    pci_device = 0;
    ready = link_up = 0;
    mac_version = RTL_GIGA_MAC_NONE;
    mac_address = 0;
    link_poll = 0;
    diag = (net_diagnostics_t){0};

    progress("Realtek: scanning PCI");
    if (!discover() || !mmio) return fail(NET_INIT_FAILURE_DISCOVERY);

    uint32_t txconfig = read32(TxConfig);
    if (txconfig == 0xFFFFFFFFu) {
        diag.last_error_status = 0xFFFF;
        return fail(NET_INIT_FAILURE_REGISTER_ACCESS);
    }

    uint32_t xid = (txconfig >> 20) & 0xFCFu;
    const r8169_chip_t* chip = find_chip(xid);
    mac_version = chip->version;
    if (mac_version == RTL_GIGA_MAC_NONE) {
        return fail(NET_INIT_FAILURE_DISCOVERY);
    }
    if (mac_version >= RTL_GIGA_MAC_VER_61) {
        debug_print("[R8169] RTL8125/8126/8127 requires the RSS driver; "
                    "not supported yet.\n");
        return fail(NET_INIT_FAILURE_DISCOVERY);
    }

    debug_print("[R8169] Chip: ");
    debug_print(chip->name ? chip->name : "unknown");
    debug_print(", XID ");
    debug_hex(xid);
    debug_print(", MMIO ");
    debug_hex((uint32_t)mmio);
    debug_print("\n");

    progress("Realtek: resetting MAC");
    if (!hw_reset()) return fail(NET_INIT_FAILURE_TIMEOUT);

    /* Disable legacy wake/ASPM state before touching the PHY. */
    unlock_config();
    write8(Config1, (uint8_t)((read8(Config1) &
              ~(Config1_LEDS1 | Config1_LEDS0 | Config1_Speed_down)) |
              Config1_PMEnable));
    write8(Config2, (uint8_t)(read8(Config2) &
              ~(Config2_ClkReqEn | Config2_MSIEnable)));
    write8(Config3, (uint8_t)(read8(Config3) &
              ~(Config3_MagicPkt | Config3_LinkUp)));
    write8(Config4, (uint8_t)(read8(Config4) & ~Config4_Jumbo_En1));
    write8(Config5, (uint8_t)(read8(Config5) &
              ~(Config5_BWF | Config5_MWF | Config5_UWF | Config5_LanWake |
                Config5_PMEStatus | Config5_ASPM_en)));
    lock_config();

    progress("Realtek: reading MAC address");
    uint8_t mac[6];
    read_mac(mac);
    if (!mac_is_valid(mac)) return fail(NET_INIT_FAILURE_REGISTER_ACCESS);
    for (uint8_t i = 0; i < 6; ++i) mac_address = (mac_address << 8) | mac[i];
    write_mac(mac);

    debug_print("[R8169] MAC: ");
    if (pci_device) debug_hex(pci_device->vendor_id);
    debug_print(":");
    if (pci_device) debug_hex(pci_device->device_id);
    debug_print(", hw ");
    debug_hex((uint32_t)(mac_address >> 16));
    debug_hex((uint32_t)mac_address);
    debug_print("\n");

    progress("Realtek: programming descriptors");
    init_rings();
    irq_mask_and_ack();
    init_rxcfg();

    unlock_config();
    write16(RxMaxSize, RX_BUFFER_SIZE + 1);
    {
        uint16_t cp = (uint16_t)(read16(CPlusCmd) & CPlusCmd_MASK);
        if (mac_version <= RTL_GIGA_MAC_VER_06) cp |= CPlusCmd_PCIMulRW;
        write16(CPlusCmd, cp);
    }
    write16(IntrMitigate, 0);
    set_early_tx_or_packet_size();
    if (mac_version <= RTL_GIGA_MAC_VER_06 && pci_device) {
        pci_write8(pci_device, 0x0d, 0x40);   /* latency timer */
        pci_write8(pci_device, 0x0e, 0x08);   /* cache line */
    }
    lock_config();

    progress("Realtek: starting DMA");
    write8(ChipCmd, CmdTxEnb | CmdRxEnb);
    init_rxcfg();
    write32(TxConfig, TX_DMA_BURST | TX_IFG |
                      (is_8168evl_up() ? TXCFG_AUTO_FIFO : 0));
    set_rx_mode();
    irq_mask_and_ack();

    progress("Realtek: initializing PHY");
    init_phy();

    ready = 1;
    diag.initialized = 1;
    update_link();
    progress("Realtek: adapter initialized");
    return 1;
}

uint8_t r8169_is_ready(void) {
    return ready;
}

void r8169_shutdown(void) {
    ready = 0;
    if (!mmio) return;
    write8(ChipCmd, 0);
    irq_mask_and_ack();
    write32(RxConfig, read32(RxConfig) & ~RX_ACCEPT_MASK);
    if (pci_device) pci_set_master(pci_device, 0);
}

uint8_t r8169_send(const uint8_t* data, uint32_t length) {
    ++diag.tx_attempts;
    if (!ready || !data || length < 14 || length > NET_MAX_FRAME_SIZE ||
        length > RX_BUFFER_SIZE) return 0;

    /* Reclaim completed descriptors before testing for room. */
    while (tx_dirty != tx_cur) {
        if (tx_ring[tx_dirty].opts1 & DescOwn) break;
        tx_ring[tx_dirty].opts1 = 0;
        tx_dirty = (uint16_t)((tx_dirty + 1) % NUM_TX_DESC);
        ++diag.tx_completed;
    }

    uint16_t next = (uint16_t)((tx_cur + 1) % NUM_TX_DESC);
    if (next == tx_dirty) { ++diag.tx_busy; return 0; }

    uint32_t padded = length < 60 ? 60 : length;
    for (uint32_t i = 0; i < padded; ++i)
        tx_buffers[tx_cur][i] = i < length ? data[i] : 0;

    r8169_desc_t* desc = &tx_ring[tx_cur];
    desc->addr_lo = (uint32_t)tx_buffers[tx_cur];
    desc->addr_hi = 0;
    desc->opts2 = 0;
    __asm__ volatile ("" ::: "memory");
    desc->opts1 = padded | DescOwn | FirstFrag | LastFrag |
                  (tx_cur == NUM_TX_DESC - 1 ? RingEnd : 0);
    __asm__ volatile ("" ::: "memory");

    tx_cur = next;
    ++diag.tx_submitted;
    write8(TxPoll, TX_POLL_NPQ);
    return 1;
}

void r8169_poll(void) {
    if (!ready) return;

    /* Transmit completion. */
    while (tx_dirty != tx_cur) {
        uint32_t status = tx_ring[tx_dirty].opts1;
        if (status & DescOwn) break;
        tx_ring[tx_dirty].opts1 = 0;
        diag.last_tx_status = (uint16_t)status;
        diag.tx_completed++;
        tx_dirty = (uint16_t)((tx_dirty + 1) % NUM_TX_DESC);
    }

    /* Link state changes slowly; sample it rather than every poll. */
    if ((link_poll++ & 127u) == 0) update_link();

    uint32_t status = read16(IntrStatus);
    if (status & IntrErrBits) {
        diag.last_error_status = (uint16_t)status;
        if (status & (0x8000 | 0x0040 | 0x0010)) diag.rx_errors++;
        if (status & 0x0008) diag.tx_errors++;
        write16(IntrStatus, (uint16_t)status);
    }

    /* Receive with a bounded budget so the desktop keeps running. */
    for (uint32_t budget = RX_POLL_BUDGET; budget; --budget) {
        r8169_desc_t* desc = &rx_ring[rx_cur];
        uint32_t opts1 = desc->opts1;
        if (opts1 & DescOwn) break;
        __asm__ volatile ("" ::: "memory");

        diag.rx_descriptor_status = (uint16_t)opts1;
        uint32_t packet_size = opts1 & 0x3FFFu;
        diag.rx_message_length = packet_size;

        uint8_t fragmented =
            (opts1 & (FirstFrag | LastFrag)) != (FirstFrag | LastFrag);
        if ((opts1 & RxRES) || fragmented || packet_size < 64 ||
            packet_size > RX_BUFFER_SIZE) {
            ++diag.rx_errors;
            ++diag.rx_invalid;
            if (opts1 & RxCRC) ++diag.rx_errors;
        } else {
            uint32_t length = packet_size - 4;   /* strip Ethernet FCS */
            diag.last_rx_length = length;
            ++diag.rx_frames;
            if (rx_handler) rx_handler(rx_buffers[rx_cur], length);
        }

        desc->opts2 = 0;
        __asm__ volatile ("" ::: "memory");
        desc->opts1 = DescOwn | RX_BUFFER_SIZE |
                      (rx_cur == NUM_RX_DESC - 1 ? RingEnd : 0);
        __asm__ volatile ("" ::: "memory");
        rx_cur = (uint16_t)((rx_cur + 1) % NUM_RX_DESC);
    }
}

void r8169_set_receive_handler(net_receive_handler_t handler) {
    rx_handler = handler;
}

uint64_t r8169_get_mac_address(void) { return mac_address; }

void r8169_set_ip_address(uint32_t address) { ip_address = address; }
uint32_t r8169_get_ip_address(void) { return ip_address; }

void r8169_get_diagnostics(net_diagnostics_t* output) {
    if (!output) return;
    *output = diag;
    output->initialized = ready;
    output->tx_index = (uint8_t)tx_cur;
    output->rx_index = (uint8_t)rx_cur;
    if (mmio) output->csr0 = read8(ChipCmd);
    output->phy_addr = phy_addr;
    output->tx_descriptor_address = (uint32_t)tx_buffers[tx_cur];
    output->tx_descriptor_status = (uint16_t)tx_ring[tx_cur].opts1;
    output->rx_descriptor_status = (uint16_t)rx_ring[rx_cur].opts1;
    output->last_rx_length = diag.last_rx_length;
    if (ready) update_link();
}

void r8169_set_init_progress(void (*progress_cb)(const char* status)) {
    init_progress = progress_cb;
}

/* BCM4401 PCI Fast Ethernet, polled DMA driver.
 * Hardware sequencing follows iPXE's b44 driver (GPL-2.0-or-later):
 * https://github.com/ipxe/ipxe/blob/master/src/drivers/net/b44.c
 * Copyright (c) 2008 Stefan Hajnoczi and Pantelis Koukousoulas;
 * Copyright (c) 2002 David S. Miller and Pekka Pietikainen;
 * Copyright (c) 2006 Broadcom Corporation; SSB work by Michael Buesch.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "net_b44.h"
#include "net_b44_regs.h"
#include "serial.h"

#define COUNT 16U
#define BUFFER_SIZE 1600U
#define RX_OFFSET 30U
#define EOT 0x10000000U
#define TX_FLAGS 0xE0000000U
#define DMA_TRANSLATION 0x40000000U
#define CORE_CLOCK 0x00010000U
#define CORE_FORCE 0x00020000U

typedef struct { uint32_t control, address; } descriptor_t;
/* The 12-bit DMA cursor requires each ring to start on a 4 KiB boundary.
 * Kernel BSS is identity mapped below 1 GiB, the device's DMA limit. */
static volatile descriptor_t rx_ring[COUNT] __attribute__((aligned(4096)));
static volatile descriptor_t tx_ring[COUNT] __attribute__((aligned(4096)));
static volatile uint8_t rx_buffers[COUNT][BUFFER_SIZE] __attribute__((aligned(64)));
static uint8_t tx_buffers[COUNT][BUFFER_SIZE] __attribute__((aligned(64)));
static uint32_t mmio, pci_address, ip;
static uint64_t mac;
static uint8_t ready, phy, tx_prod, tx_cons, rx_cons;
static uint32_t link_poll;
static uint8_t core_accessible;
static net_receive_handler_t handler;
static net_diagnostics_t diag;
static void (*init_progress)(const char* status);
void b44_set_init_progress(void (*progress)(const char* status)) {
    init_progress = progress;
}
static void progress(const char* status) {
    /* Paint before logging: even a UART failure leaves a useful screen. */
    if (init_progress) init_progress(status);
    debug_print(status);
    debug_print("\n");
}

static uint32_t read_reg(uint32_t reg) {
    return *(volatile uint32_t*)(mmio + reg);
}
static void write_reg(uint32_t reg, uint32_t value) {
    *(volatile uint32_t*)(mmio + reg) = value;
    (void)read_reg(reg); /* drain posted PCI writes */
}
static void delay_us(uint32_t count) {
    while (count--) outb(0x80, 0);
}
static uint8_t wait_reg(uint32_t reg, uint32_t mask, uint8_t set) {
    for (uint32_t i = 0; i < 100; ++i) {
        uint32_t value = read_reg(reg);
        if (value == 0xFFFFFFFFU) return 0;
        if (!!(value & mask) == set) return 1;
        delay_us(10);
    }
    return 0;
}
static uint32_t pci_read(uint8_t reg) {
    outl(0xCF8, pci_address | (reg & 0xFC));
    return inl(0xCFC);
}
static void pci_write(uint8_t reg, uint32_t value) {
    outl(0xCF8, pci_address | (reg & 0xFC));
    outl(0xCFC, value);
}
static uint8_t discover(void) {
    for (uint32_t bus = 0; bus < 256; ++bus)
        for (uint32_t slot = 0; slot < 32; ++slot)
            for (uint32_t fn = 0; fn < 8; ++fn) {
                pci_address = 0x80000000U | bus << 16 | slot << 11 | fn << 8;
                uint32_t id = pci_read(0);
                if (id != 0x440114E4U && id != 0x170C14E4U && id != 0x440214E4U)
                    continue;
                uint32_t bar = pci_read(0x10);
                if (!bar || bar == 0xFFFFFFFFU || (bar & 7)) return 0;
                mmio = bar & ~15U;
                /* BCM4401's fixed power-management register: enter D0.
                 * PME status is W1C, so leave bit 15 clear. */
                progress("BCM4401: powering PCI device");
                uint16_t pm = (uint16_t)pci_read(0x44);
                outl(0xCF8, pci_address | 0x44);
                outw(0xCFC, pm & ~0x8003U);
                delay_us(10000);
                /* Write only command, never the write-one-to-clear PCI status. */
                outl(0xCF8, pci_address | 4);
                outw(0xCFC, (uint16_t)pci_read(4) | 6);
                return 1;
            }
    return 0;
}
static uint8_t mdio(uint8_t reg, uint16_t* value, uint8_t write) {
    write_reg(B44_EMAC_ISTAT, EMAC_INT_MII);
    write_reg(B44_MDIO_DATA, 0x40020000U | (write ? 1U : 2U) << 28 |
              (uint32_t)phy << 23 | (uint32_t)reg << 18 | (write ? *value : 0));
    if (!wait_reg(B44_EMAC_ISTAT, EMAC_INT_MII, 1)) return 0;
    if (!write) *value = (uint16_t)read_reg(B44_MDIO_DATA);
    return 1;
}
static uint8_t phy_write(uint8_t reg, uint16_t value) {
    return mdio(reg, &value, 1);
}
static void update_link(void) {
    uint16_t status, local, remote;
    /* BMSR link status is latched low; read twice. */
    if (!mdio(1, &status, 0) || !mdio(1, &status, 0) || !(status & 4)) return;
    if ((status & 0x20) && mdio(4, &local, 0) && mdio(5, &remote, 0)) {
        uint16_t common = local & remote;
        uint8_t full = (common & 0x100) || (!(common & 0x80) && (common & 0x40));
        write_reg(B44_TX_CTRL, full ? TX_CTRL_DUPLEX : 0);
    }
}
/* Only the SSB agent registers are safe when the core is in reset or
 * clock-gated. Touching the MAC in that state can stall a real PCI bus. */
static uint8_t disable_core(void) {
    uint32_t state = read_reg(B44_SBTMSLOW);
    if (state == 0xFFFFFFFFU) return 0;
    if (state & SBTMSLOW_RESET) return 1;
    write_reg(B44_SBTMSLOW, CORE_CLOCK | SBTMSLOW_REJECT);
    if (!wait_reg(B44_SBTMSLOW, SBTMSLOW_REJECT, 1) ||
        !wait_reg(B44_SBTMSHIGH, SBTMSHIGH_BUSY, 0)) return 0;
    write_reg(B44_SBTMSLOW, CORE_CLOCK | CORE_FORCE | SBTMSLOW_REJECT | SBTMSLOW_RESET);
    delay_us(10);
    write_reg(B44_SBTMSLOW, SBTMSLOW_REJECT | SBTMSLOW_RESET);
    delay_us(10);
    return 1;
}
void b44_shutdown(void) {
    ready = 0;
    if (!mmio || !core_accessible) return;
    write_reg(B44_IMASK, 0);
    write_reg(B44_ENET_CTRL, ENET_CTRL_DISABLE);
    (void)wait_reg(B44_ENET_CTRL, ENET_CTRL_DISABLE, 0);
    write_reg(B44_DMATX_CTRL, 0);
    write_reg(B44_DMARX_CTRL, 0);
    (void)disable_core();
    core_accessible = 0;
}
static uint8_t fail(uint8_t reason) {
    debug_print("[B44] Initialization failed: ");
    debug_hex(reason);
    debug_print("\n");
    progress("BCM4401: initialization failed, stopping");
    diag.init_failure = reason;
    b44_shutdown();
    return 0;
}
uint8_t b44_init(void) {
    progress("BCM4401: entering driver");
    if (mmio) b44_shutdown();
    mmio = 0;
    core_accessible = 0;
    diag = (net_diagnostics_t){0};
    mac = 0;
    tx_prod = tx_cons = rx_cons = 0;
    link_poll = 0;
    progress("BCM4401: scanning PCI");
    if (!discover()) return fail(NET_INIT_FAILURE_DISCOVERY);
    debug_print("[B44] BAR0: ");
    debug_hex(mmio);
    debug_print("\n");
    progress("BCM4401: selecting Ethernet window");
    /* BAR0 window selects the Ethernet core; SPROM occupies the next 4 KiB. */
    pci_write(0x80, 0x18000000U);
    if (read_reg(B44_SBIDHIGH) == 0xFFFFFFFFU)
        return fail(NET_INIT_FAILURE_REGISTER_ACCESS);
    progress("BCM4401: reading SPROM");
    for (uint32_t i = 0; i < 3; ++i) {
        uint16_t word = *(volatile uint16_t*)(mmio + 4096 + 78 + i * 2);
        mac = (mac << 16) | word;
    }
    phy = *(volatile uint16_t*)(mmio + 4096 + 90) & 31;
    if (!mac || mac == 0xFFFFFFFFFFFFULL || (mac & 0x010000000000ULL))
        return fail(NET_INIT_FAILURE_REGISTER_ACCESS);
    /* Set up the backplane-to-PCI DMA translation, then restore Ethernet. */
    progress("BCM4401: configuring PCI bridge");
    pci_write(0x80, 0x18002000U);
    write_reg(SSB_PCI_TRANS_2, read_reg(SSB_PCI_TRANS_2) | SSB_PCI_PREF | SSB_PCI_BURST);
    pci_write(0x80, 0x18000000U);
    progress("BCM4401: reading core state");
    /* Firmware may leave the Ethernet core asleep. Never call the MAC
     * shutdown path until SSB reports a running core. */
    uint32_t state = read_reg(B44_SBTMSLOW);
    if ((state & (CORE_CLOCK | SBTMSLOW_RESET | SBTMSLOW_REJECT)) == CORE_CLOCK) {
        core_accessible = 1;
        progress("BCM4401: stopping firmware MAC");
        b44_shutdown();
    }
    progress("BCM4401: disabling core");
    if (!disable_core()) return fail(NET_INIT_FAILURE_TIMEOUT);
    progress("BCM4401: enabling core clocks");
    write_reg(B44_SBTMSLOW, CORE_CLOCK | CORE_FORCE | SBTMSLOW_RESET);
    delay_us(10);
    if (read_reg(B44_SBTMSHIGH) & SBTMSHIGH_SERR) write_reg(B44_SBTMSHIGH, 0);
    write_reg(B44_SBIMSTATE, read_reg(B44_SBIMSTATE) & ~SBIMSTATE_BAD);
    write_reg(B44_SBTMSLOW, CORE_CLOCK | CORE_FORCE);
    delay_us(10);
    write_reg(B44_SBTMSLOW, CORE_CLOCK);
    delay_us(10);
    if (!wait_reg(B44_SBTMSLOW, SBTMSLOW_RESET | SBTMSLOW_REJECT, 0))
        return fail(NET_INIT_FAILURE_TIMEOUT);
    core_accessible = 1;
    progress("BCM4401: initializing PHY");
    write_reg(B44_IMASK, 0);
    write_reg(B44_EMAC_IMASK, 0);
    write_reg(B44_MDIO_CTRL, 0x8D);
    uint32_t dev = read_reg(B44_DEVCTRL);
    if (!(dev & DEVCTRL_IPP)) write_reg(B44_ENET_CTRL, ENET_CTRL_EPSEL);
    else if (dev & DEVCTRL_EPR) {
        write_reg(B44_DEVCTRL, dev & ~DEVCTRL_EPR);
        delay_us(100);
    }
    write_reg(B44_MAC_CTRL, MAC_CTRL_CRC32_ENAB | MAC_CTRL_PHY_LEDCTRL);
    progress("BCM4401: resetting PHY");
    if (!phy_write(0, 0x8000)) return fail(NET_INIT_FAILURE_TIMEOUT);
    uint16_t value = 0x8000;
    uint32_t attempt;
    for (attempt = 0; attempt < 1000; ++attempt) {
        if (!mdio(0, &value, 0)) return fail(NET_INIT_FAILURE_TIMEOUT);
        if (!(value & 0x8000)) break;
        delay_us(100);
    }
    if (attempt == 1000 || !mdio(2, &value, 0) || !value || value == 0xFFFF)
        return fail(NET_INIT_FAILURE_TIMEOUT);
    diag.phy_id = value;
    if (!phy_write(4, 0x01E1) || !phy_write(0, 0x1200))
        return fail(NET_INIT_FAILURE_TIMEOUT);
    progress("BCM4401: programming MAC filter");
    /* Clear all CAM entries, then install our unicast address in entry zero. */
    write_reg(B44_CAM_CTRL, 0);
    for (uint32_t i = 0; i < 64; ++i) {
        write_reg(B44_CAM_DATA_LO, i ? 0 : (uint32_t)mac);
        write_reg(B44_CAM_DATA_HI, i ? 0 : CAM_DATA_HI_VALID | (uint32_t)(mac >> 32));
        write_reg(B44_CAM_CTRL, CAM_CTRL_WRITE | i << CAM_CTRL_INDEX_SHIFT);
        if (!wait_reg(B44_CAM_CTRL, CAM_CTRL_BUSY, 0)) return fail(NET_INIT_FAILURE_TIMEOUT);
    }
    write_reg(B44_CAM_CTRL, CAM_CTRL_ENABLE);
    write_reg(B44_RXCONFIG, RXCONFIG_ALLMULTI);
    for (uint32_t i = 0; i < COUNT; ++i) {
        rx_buffers[i][0] = rx_buffers[i][1] = rx_buffers[i][2] = rx_buffers[i][3] = 0;
        rx_ring[i].control = (BUFFER_SIZE - RX_OFFSET) | (i == COUNT - 1 ? EOT : 0);
        rx_ring[i].address = (uint32_t)rx_buffers[i] | DMA_TRANSLATION;
        tx_ring[i].control = tx_ring[i].address = 0;
    }
    progress("BCM4401: starting DMA");
    write_reg(B44_RXMAXLEN, 1536);
    write_reg(B44_TXMAXLEN, 1536);
    write_reg(B44_TX_HIWMARK, 56);
    write_reg(B44_RCV_LAZY, 1U << 24);
    write_reg(B44_MIB_CTRL, MIB_CTRL_CLR_ON_READ);
    __asm__ volatile ("" ::: "memory");
    write_reg(B44_DMATX_ADDR, (uint32_t)tx_ring | DMA_TRANSLATION);
    write_reg(B44_DMATX_PTR, 0);
    write_reg(B44_DMATX_CTRL, 1);
    write_reg(B44_DMARX_ADDR, (uint32_t)rx_ring | DMA_TRANSLATION);
    write_reg(B44_DMARX_CTRL, 1 | RX_OFFSET << 1);
    write_reg(B44_DMARX_PTR, COUNT * sizeof(descriptor_t));
    write_reg(B44_ISTAT, 0xFFFFFFFFU);
    write_reg(B44_ENET_CTRL, read_reg(B44_ENET_CTRL) | ENET_CTRL_ENABLE);
    ready = 1;
    progress("BCM4401: checking PHY link");
    update_link();
    progress("BCM4401: adapter initialized");
    return 1;
}
static void reap_tx(void) {
    uint32_t pending = (read_reg(B44_DMATX_STAT) & 0xFFF) / sizeof(descriptor_t);
    if (pending >= COUNT) return;
    while (tx_cons != pending && tx_cons != tx_prod) {
        tx_ring[tx_cons].control = 0;
        tx_cons = (tx_cons + 1) % COUNT;
        ++diag.tx_completed;
    }
}
uint8_t b44_send(const uint8_t* data, uint32_t length) {
    ++diag.tx_attempts;
    if (!ready || !data || length < 14 || length > NET_MAX_FRAME_SIZE) return 0;
    reap_tx();
    uint8_t next = (tx_prod + 1) % COUNT;
    if (next == tx_cons) { ++diag.tx_busy; return 0; }
    uint32_t padded = length < 60 ? 60 : length;
    for (uint32_t i = 0; i < padded; ++i) tx_buffers[tx_prod][i] = i < length ? data[i] : 0;
    tx_ring[tx_prod].address = (uint32_t)tx_buffers[tx_prod] | DMA_TRANSLATION;
    tx_ring[tx_prod].control = padded | TX_FLAGS | (tx_prod == COUNT - 1 ? EOT : 0);
    __asm__ volatile ("" ::: "memory");
    tx_prod = next;
    write_reg(B44_DMATX_PTR, tx_prod * sizeof(descriptor_t));
    ++diag.tx_submitted;
    return 1;
}
void b44_poll(void) {
    if (!ready) return;
    uint32_t status = read_reg(B44_ISTAT);
    if (status & ISTAT_ERRORS) {
        diag.last_error_status = (uint16_t)status;
        ++diag.rx_errors;
        diag.tx_errors += (tx_prod + COUNT - tx_cons) % COUNT;
        /* Stop DMA on fatal errors; retain diagnostics for netdiag. */
        b44_shutdown();
        return;
    }
    if ((link_poll++ & 127U) == 0) update_link();
    reap_tx();
    uint32_t pending = (read_reg(B44_DMARX_STAT) & 0xFFF) / sizeof(descriptor_t);
    if (pending >= COUNT) return;
    for (uint32_t budget = 8; budget && rx_cons != pending; --budget) {
        volatile uint16_t* header = (volatile uint16_t*)rx_buffers[rx_cons];
        uint16_t length = header[0], flags = header[1];
        if (!length) break; /* DMA header may lag the hardware cursor. */
        __asm__ volatile ("" ::: "memory");
        diag.rx_descriptor_status = flags;
        diag.rx_message_length = length;
        if (length < 18 || length > NET_MAX_FRAME_SIZE + 4 || (flags & 0x1F)) {
            ++diag.rx_errors;
            ++diag.rx_invalid;
        } else {
            ++diag.rx_frames;
            diag.last_rx_length = length - 4;
            if (handler) handler((const uint8_t*)rx_buffers[rx_cons] + RX_OFFSET, length - 4);
        }
        header[0] = header[1] = 0;
        __asm__ volatile ("" ::: "memory");
        /* Advance the last-posted cursor only after the buffer is reusable. */
        write_reg(B44_DMARX_PTR, rx_cons * sizeof(descriptor_t));
        rx_cons = (rx_cons + 1) % COUNT;
    }
    write_reg(B44_ISTAT, status);
}
uint8_t b44_is_ready(void) { return ready; }
void b44_set_receive_handler(net_receive_handler_t fn) { handler = fn; }
uint64_t b44_get_mac_address(void) { return mac; }
void b44_set_ip_address(uint32_t value) { ip = value; }
uint32_t b44_get_ip_address(void) { return ip; }
void b44_get_diagnostics(net_diagnostics_t* output) {
    if (!output) return;
    *output = diag;
    output->initialized = ready;
    output->tx_index = tx_prod;
    output->rx_index = rx_cons;
    output->phy_addr = phy;
    if (ready) update_link();
}

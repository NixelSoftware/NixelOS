#include "pcnet32.h"
#include "../etherframe.h"

#define PCNET_DWIO_RDP    0x10
#define PCNET_DWIO_RAP    0x14
#define PCNET_DWIO_RESET  0x18
#define PCNET_DWIO_BDP    0x1C

#define PCNET_WIO_RESET   0x14

#define CSR0        0
#define CSR0_INIT   (1 << 0)
#define CSR0_STRT   (1 << 1)
#define CSR0_STOP   (1 << 2)
#define CSR0_TXPOLL (1 << 3)
#define CSR0_INTEN  (1 << 6)
#define CSR0_INTR   (1 << 7)
#define CSR0_IDON   (1 << 8)
#define CSR0_TINT   (1 << 9)
#define CSR0_RINT   (1 << 10)
#define CSR0_MERR   (1 << 11)
#define CSR0_MISS   (1 << 12)
#define CSR0_BABL   (1 << 14)
#define CSR0_ERR    (1 << 15)

#define CSR4        4
#define CSR4_APAD_XMT (1 << 11)

#define DESC_OWN    (1 << 15)
#define DESC_ERR    (1 << 14)
#define DESC_STP    (1 << 9)
#define DESC_ENP    (1 << 8)

struct pcnet_rx_desc {
    uint32_t base;
    int16_t  buf_length;
    uint16_t status;
    uint32_t msg_length;
    uint32_t reserved;
} __attribute__((packed, aligned(16)));

struct pcnet_tx_desc {
    uint32_t base;
    int16_t  length;
    uint16_t status;
    uint32_t misc;
    uint32_t reserved;
} __attribute__((packed, aligned(16)));

struct pcnet_init_block {
    uint16_t mode;
    uint16_t rlen_tlen;
    uint8_t  mac[6];
    uint16_t reserved;
    uint32_t filter[2];
    uint32_t rx_ring_ptr;
    uint32_t tx_ring_ptr;
} __attribute__((packed, aligned(16)));

static struct pcnet_init_block s_init_block;
static struct pcnet_rx_desc   s_rx_ring[PCNET_RX_BUFFERS];
static struct pcnet_tx_desc   s_tx_ring[PCNET_TX_BUFFERS];

static uint8_t s_rx_buffers[PCNET_RX_BUFFERS][PCNET_BUF_SIZE] __attribute__((aligned(16)));
static uint8_t s_tx_buffers[PCNET_TX_BUFFERS][PCNET_BUF_SIZE] __attribute__((aligned(16)));

static uint16_t s_io_base = 0;
static uint8_t  s_mac[6] = {0};
static uint32_t s_rx_idx = 0;
static uint32_t s_tx_idx = 0;

static inline void outl_local(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl_local(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void write_csr(uint16_t io, uint32_t reg, uint32_t val) {
    outl_local(io + PCNET_DWIO_RAP, reg);
    outl_local(io + PCNET_DWIO_RDP, val);
}

static uint32_t read_csr(uint16_t io, uint32_t reg) {
    outl_local(io + PCNET_DWIO_RAP, reg);
    return inl_local(io + PCNET_DWIO_RDP);
}

static void write_bcr(uint16_t io, uint32_t reg, uint32_t val) {
    outl_local(io + PCNET_DWIO_RAP, reg);
    outl_local(io + PCNET_DWIO_BDP, val);
}

static uint32_t read_bcr(uint16_t io, uint32_t reg) {
    outl_local(io + PCNET_DWIO_RAP, reg);
    return inl_local(io + PCNET_DWIO_BDP);
}

const uint8_t *pcnet_get_mac(void) {
    return s_mac;
}

int pcnet_init(uint16_t io_base, uint8_t irq) {
    (void)irq;
    s_io_base = io_base;

    inl_local(io_base + PCNET_DWIO_RESET);
    inw(io_base + PCNET_WIO_RESET);

    write_bcr(io_base, 20, 0x0002);

    for (int i = 0; i < 6; i++) {
        s_mac[i] = inb(io_base + i);
    }

    s_rx_idx = 0;
    for (int i = 0; i < PCNET_RX_BUFFERS; i++) {
        s_rx_ring[i].base = (uint32_t)&s_rx_buffers[i][0];
        s_rx_ring[i].buf_length = (int16_t)(-PCNET_BUF_SIZE);
        s_rx_ring[i].status = DESC_OWN;
        s_rx_ring[i].msg_length = 0;
        s_rx_ring[i].reserved = 0;
    }

    s_tx_idx = 0;
    for (int i = 0; i < PCNET_TX_BUFFERS; i++) {
        s_tx_ring[i].base = (uint32_t)&s_tx_buffers[i][0];
        s_tx_ring[i].length = 0;
        s_tx_ring[i].status = 0;
        s_tx_ring[i].misc = 0;
        s_tx_ring[i].reserved = 0;
    }

    s_init_block.mode = 0x0000;
    s_init_block.rlen_tlen = (3 << 4) | (3 << 12);
    for (int i = 0; i < 6; i++) {
        s_init_block.mac[i] = s_mac[i];
    }
    s_init_block.reserved = 0;
    s_init_block.filter[0] = 0;
    s_init_block.filter[1] = 0;
    s_init_block.rx_ring_ptr = (uint32_t)&s_rx_ring[0];
    s_init_block.tx_ring_ptr = (uint32_t)&s_tx_ring[0];

    write_csr(io_base, CSR0, CSR0_STOP);

    uint32_t init_addr = (uint32_t)&s_init_block;
    write_csr(io_base, 1, init_addr & 0xFFFF);
    write_csr(io_base, 2, (init_addr >> 16) & 0xFFFF);

    write_csr(io_base, CSR4, CSR4_APAD_XMT);

    write_csr(io_base, CSR0, CSR0_INIT);

    int timeout = 100000;
    while (!(read_csr(io_base, CSR0) & CSR0_IDON)) {
        if (--timeout <= 0) {
            return -1;
        }
    }

    write_csr(io_base, CSR0, CSR0_IDON | CSR0_STRT | CSR0_INTEN);

    return 0;
}

void pcnet_send_packet(const uint8_t *buffer, uint16_t len) {
    if (!s_io_base || len > PCNET_BUF_SIZE) {
        return;
    }

    uint32_t entry = s_tx_idx;

    if (s_tx_ring[entry].status & DESC_OWN) {
        return;
    }

    for (uint16_t i = 0; i < len; i++) {
        s_tx_buffers[entry][i] = buffer[i];
    }

    s_tx_ring[entry].length = (int16_t)(-len);
    s_tx_ring[entry].misc = 0;
    s_tx_ring[entry].status = DESC_OWN | DESC_STP | DESC_ENP;

    s_tx_idx = (s_tx_idx + 1) % PCNET_TX_BUFFERS;

    write_csr(s_io_base, CSR0, CSR0_TXPOLL | CSR0_INTEN);
}

void pcnet_poll(void) {
    if (!s_io_base) return;

    while (!(s_rx_ring[s_rx_idx].status & DESC_OWN)) {
        uint16_t status = s_rx_ring[s_rx_idx].status;

        if ((status & (DESC_STP | DESC_ENP)) && !(status & DESC_ERR)) {
            uint16_t frame_len = (s_rx_ring[s_rx_idx].msg_length & 0x0FFF) - 4;
            if (frame_len <= PCNET_BUF_SIZE) {
                etherframe_receive(s_rx_buffers[s_rx_idx], frame_len);
            }
        }

        s_rx_ring[s_rx_idx].buf_length = (int16_t)(-PCNET_BUF_SIZE);
        s_rx_ring[s_rx_idx].status = DESC_OWN;

        s_rx_idx = (s_rx_idx + 1) % PCNET_RX_BUFFERS;
    }
}

void pcnet_handle_interrupt(void) {
    if (!s_io_base) return;

    uint32_t csr0 = read_csr(s_io_base, CSR0);

    write_csr(s_io_base, CSR0, csr0);

    if (csr0 & CSR0_RINT) {
        pcnet_poll();
    }
}
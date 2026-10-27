#include "pcnet.h"
#include "../../drivers/pci.h"

#define PCNET_SW_RDP 0x10
#define PCNET_SW_RAP 0x12
#define PCNET_SW_RESET 0x14
#define PCNET_SW_BDP 0x16
#define PCNET_DW_RDP 0x10
#define PCNET_DW_RAP 0x14
#define PCNET_DW_RESET 0x18
#define PCNET_DW_BDP 0x1C

#define CSR0_INIT 0x0001
#define CSR0_STRT 0x0002
#define CSR0_STOP 0x0004
#define CSR0_TDMD 0x0008
#define CSR0_TXPOLL CSR0_TDMD
#define CSR0_IENA 0x0040
#define CSR4_APAD_XMT 0x0800
#define MODE_PROM 0x8000
#define CSR0_TINT 0x0200
#define CSR0_RINT 0x0400
#define CSR0_IDON 0x0100
#define CSR0_ERR  0x8000

#define BCR20_SSTYLE_32 0x0102

#define DESCRIPTOR_OWN 0x8000
#define DESCRIPTOR_ERR 0x4000
#define DESCRIPTOR_STP 0x0200
#define DESCRIPTOR_ENP 0x0100

#define RING_COUNT 8
#define BUFFER_SIZE 1536
#define PCNET_VENDOR_ID 0x1022
#define PCNET_DEVICE_ID 0x2000

typedef struct {
    uint16_t mode;
    uint16_t ring_lengths;
    uint8_t physical_address[6];
    uint16_t reserved;
    uint32_t logical_address_filter[2];
    uint32_t receive_ring;
    uint32_t transmit_ring;
} __attribute__((packed, aligned(16))) pcnet_init_block_t;

typedef struct {
    uint32_t address;
    int16_t buffer_length;
    uint16_t status;
    uint32_t message_length;
    uint32_t reserved;
} __attribute__((packed, aligned(16))) pcnet_rx_descriptor_t;

typedef struct {
    uint32_t address;
    int16_t length;
    uint16_t status;
    uint32_t misc;
    uint32_t reserved;
} __attribute__((packed, aligned(16))) pcnet_tx_descriptor_t;

static uint16_t net_io_base;
static uint8_t net_ready;
static uint8_t net_tx_index;
static uint8_t net_rx_index;
static uint64_t net_mac;
static uint32_t net_ip;
static net_receive_handler_t net_handler;
static net_diagnostics_t diagnostics;
static uint8_t transmit_pending[RING_COUNT];

static pcnet_init_block_t init_block __attribute__((aligned(16)));
static pcnet_rx_descriptor_t receive_ring[RING_COUNT] __attribute__((aligned(16)));
static pcnet_tx_descriptor_t transmit_ring[RING_COUNT] __attribute__((aligned(16)));
static uint8_t receive_buffers[RING_COUNT][BUFFER_SIZE] __attribute__((aligned(16)));
static uint8_t transmit_buffers[RING_COUNT][BUFFER_SIZE] __attribute__((aligned(16)));

static uint16_t pcnet_sw_read(uint16_t register_number) {
    outw(net_io_base + PCNET_SW_RAP, register_number);
    return inw(net_io_base + PCNET_SW_RDP);
}

static uint32_t pcnet_read(uint32_t register_number) {
    outl(net_io_base + PCNET_DW_RAP, register_number);
    return inl(net_io_base + PCNET_DW_RDP);
}

static void pcnet_write(uint32_t register_number, uint32_t value) {
    outl(net_io_base + PCNET_DW_RAP, register_number);
    outl(net_io_base + PCNET_DW_RDP, value);
}

static void pcnet_start(void) {
    pcnet_write(0, CSR0_STRT | CSR0_IENA);
}

static void pcnet_sw_bcr_write(uint16_t register_number, uint16_t value) {
    outw(net_io_base + PCNET_SW_RAP, register_number);
    outw(net_io_base + PCNET_SW_BDP, value);
}

static void pcnet_enable_dwio(void) {
    outl(net_io_base + PCNET_DW_RDP, 0);
}

static uint16_t find_pcnet_io_base(void) {
    for (uint32_t index = 0; index < pci_device_count(); index++) {
        const pci_device_t* dev = pci_device_at(index);
        uint8_t bus = dev->bus, slot = dev->slot, function = dev->function;
        uint32_t identity = pci_config_read32((uint8_t)bus, slot, function, 0);
        uint16_t vendor = (uint16_t)identity;
        uint16_t device = (uint16_t)(identity >> 16);
        uint32_t bar;

        if (vendor != PCNET_VENDOR_ID || device != PCNET_DEVICE_ID)
            continue;
        bar = pci_config_read32((uint8_t)bus, slot, function, 0x10);
        if (!(bar & 1) || (bar & 0xFFFFFFFCU) == 0)
            return 0;
        pci_config_write16((uint8_t)bus, slot, function, 4, 0x0005);
        return (uint16_t)(bar & 0xFFFC);

    }
    return 0;
}

static uint32_t aligned_address(const void* address) {
    return ((uint32_t)address + 15U) & ~15U;
}

static uint64_t read_mac(void) {
    uint16_t word0 = inw(net_io_base);
    uint16_t word1 = inw(net_io_base + 2);
    uint16_t word2 = inw(net_io_base + 4);

    return ((uint64_t)(word2 >> 8) << 40)
         | ((uint64_t)(word2 & 0xFF) << 32)
         | ((uint64_t)(word1 >> 8) << 24)
         | ((uint64_t)(word1 & 0xFF) << 16)
         | ((uint64_t)(word0 >> 8) << 8)
         | (uint64_t)(word0 & 0xFF);
}

uint8_t net_init(uint16_t io_base) {
    if (io_base == 0) io_base = find_pcnet_io_base();
    if (io_base == 0) {
        net_reset_diagnostics();
        diagnostics.init_failure = NET_INIT_FAILURE_DISCOVERY;
        return 0;
    }

    net_io_base = io_base;
    net_ready = 0;
    net_tx_index = 0;
    net_rx_index = 0;
    net_mac = read_mac();
    net_reset_diagnostics();
    diagnostics.io_base = io_base;

    inw(net_io_base + PCNET_SW_RESET);
    diagnostics.sw_csr0 = pcnet_sw_read(0);
    pcnet_sw_bcr_write(20, BCR20_SSTYLE_32);
    pcnet_enable_dwio();
    diagnostics.dw_csr0 = pcnet_read(0);
    if (pcnet_read(0) == 0xFFFFU) {
        diagnostics.init_failure = NET_INIT_FAILURE_REGISTER_ACCESS;
        diagnostics.last_error_status = 0xFFFF;
        return 0;
    }

    pcnet_write(0, CSR0_STOP);

    init_block.mode = MODE_PROM;
    init_block.ring_lengths = (uint16_t)((3U << 4) | (3U << 12));
    init_block.reserved = 0;
    for (uint8_t i = 0; i < 2; i++) init_block.logical_address_filter[i] = 0;

    for (uint8_t i = 0; i < 6; i++) {
        init_block.physical_address[i] = (uint8_t)(net_mac >> (i * 8));
    }

    init_block.receive_ring = aligned_address(receive_ring);
    init_block.transmit_ring = aligned_address(transmit_ring);

    for (uint8_t i = 0; i < RING_COUNT; i++) {
        receive_ring[i].address = aligned_address(receive_buffers[i]);
        receive_ring[i].buffer_length = (int16_t)-BUFFER_SIZE;
        receive_ring[i].status = DESCRIPTOR_OWN;
        receive_ring[i].message_length = 0;
        receive_ring[i].reserved = 0;

        transmit_ring[i].address = aligned_address(transmit_buffers[i]);
        transmit_ring[i].length = 0;
        transmit_ring[i].status = 0;
        transmit_ring[i].misc = 0;
        transmit_ring[i].reserved = 0;
        transmit_pending[i] = 0;
    }

    pcnet_write(1, (uint16_t)((uint32_t)&init_block & 0xFFFF));
    pcnet_write(2, (uint16_t)(((uint32_t)&init_block >> 16) & 0xFFFF));
    pcnet_write(4, CSR4_APAD_XMT);
    pcnet_write(0, CSR0_INIT);

    for (volatile uint32_t timeout = 0; timeout < 1000000; timeout++) {
        uint32_t status = pcnet_read(0);
        if (status != 0xFFFFU && (status & CSR0_IDON)) {
            pcnet_write(0, CSR0_IDON);
            pcnet_start();
            net_ready = 1;
            diagnostics.initialized = 1;
            return 1;
        }
    }

    pcnet_write(0, CSR0_STOP);
    diagnostics.init_failure = NET_INIT_FAILURE_TIMEOUT;
    diagnostics.last_error_status = (uint16_t)pcnet_read(0);
    return 0;
}

void net_shutdown(void) {
    if (!net_ready) return;
    pcnet_write(0, CSR0_STOP);
    inw(net_io_base + PCNET_SW_RESET);
    net_ready = 0;
}

uint8_t net_is_ready(void) {
    return net_ready;
}

uint8_t net_send(const uint8_t* data, uint32_t length) {
    pcnet_tx_descriptor_t* descriptor;
    uint8_t* destination;

    diagnostics.tx_attempts++;
    if (!net_ready || !data || length == 0 || length > NET_MAX_FRAME_SIZE) {
        return 0;
    }

    descriptor = &transmit_ring[net_tx_index];
    if (descriptor->status & DESCRIPTOR_OWN) {
        diagnostics.tx_busy++;
        return 0;
    }

    destination = (uint8_t*)descriptor->address;
    for (uint32_t i = 0; i < length; i++) destination[i] = data[i];

    descriptor->length = (int16_t)-((int32_t)length);
    descriptor->misc = 0;
    descriptor->status = DESCRIPTOR_OWN | DESCRIPTOR_STP | DESCRIPTOR_ENP;
    transmit_pending[net_tx_index] = 1;
    net_tx_index = (uint8_t)((net_tx_index + 1) % RING_COUNT);
    diagnostics.tx_submitted++;
    pcnet_write(0, CSR0_TXPOLL | CSR0_STRT);
    return 1;
}

void net_poll(void) {
    uint32_t status;
    uint8_t processed = 0;

    if (!net_ready) return;

    status = pcnet_read(0);
    diagnostics.csr0 = status;
    diagnostics.rx_descriptor_status = receive_ring[net_rx_index].status;
    diagnostics.rx_message_length = receive_ring[net_rx_index].message_length;
    if (status == 0xFFFFU) {
        diagnostics.last_error_status = status;
        diagnostics.rx_errors++;
        return;
    }
    if (status & CSR0_STOP) {
        pcnet_start();
        status = pcnet_read(0);
        diagnostics.csr0 = status;
    }
    if (status & CSR0_ERR) {
        diagnostics.last_error_status = status;
        diagnostics.rx_errors++;
        pcnet_write(0, status);
    }

    for (uint8_t i = 0; i < RING_COUNT; i++) {
        if (transmit_pending[i]
            && !(transmit_ring[i].status & DESCRIPTOR_OWN)) {
            transmit_pending[i] = 0;
            diagnostics.tx_completed++;
            diagnostics.last_tx_status = transmit_ring[i].status;
            diagnostics.last_tx_misc = transmit_ring[i].misc;
            if (transmit_ring[i].status & DESCRIPTOR_ERR)
                diagnostics.tx_errors++;
        }
    }

    while (!(receive_ring[net_rx_index].status & DESCRIPTOR_OWN)
           && processed < RING_COUNT) {
        pcnet_rx_descriptor_t* descriptor = &receive_ring[net_rx_index];
        uint32_t length = descriptor->message_length & 0x0FFF;

        diagnostics.last_rx_length = length;
        if (!(descriptor->status & DESCRIPTOR_ERR)
            && (descriptor->status & (DESCRIPTOR_STP | DESCRIPTOR_ENP))
                == (DESCRIPTOR_STP | DESCRIPTOR_ENP)
            && length >= 4 && length <= BUFFER_SIZE) {
            length -= 4;
            diagnostics.rx_frames++;
            if (net_handler) {
                net_handler((const uint8_t*)descriptor->address, length);
            }
        } else {
            diagnostics.rx_invalid++;
        }

        descriptor->buffer_length = (int16_t)-BUFFER_SIZE;
        descriptor->status = DESCRIPTOR_OWN;
        net_rx_index = (uint8_t)((net_rx_index + 1) % RING_COUNT);
        processed++;
    }

    pcnet_write(0, (uint16_t)(status & (CSR0_RINT | CSR0_TINT)));
}

void net_set_receive_handler(net_receive_handler_t handler) {
    net_handler = handler;
}

uint64_t net_get_mac_address(void) {
    return net_mac;
}

void net_set_ip_address(uint32_t address) {
    net_ip = address;
}

uint32_t net_get_ip_address(void) {
    return net_ip;
}

void net_get_diagnostics(net_diagnostics_t* output) {
    if (!output) return;
    *output = diagnostics;
    output->initialized = net_ready;
    output->tx_index = net_tx_index;
    output->rx_index = net_rx_index;
    output->csr0 = net_io_base ? (uint16_t)pcnet_read(0) : 0;
    output->tx_descriptor_address = transmit_ring[net_tx_index].address;
    output->tx_descriptor_length = transmit_ring[net_tx_index].length;
    output->tx_descriptor_status = transmit_ring[net_tx_index].status;
}

void net_reset_diagnostics(void) {
    diagnostics.initialized = 0;
    diagnostics.io_base = net_io_base;
    diagnostics.csr0 = 0;
    diagnostics.tx_index = 0;
    diagnostics.rx_index = 0;
    diagnostics.tx_attempts = 0;
    diagnostics.tx_submitted = 0;
    diagnostics.tx_busy = 0;
    diagnostics.tx_completed = 0;
    diagnostics.tx_errors = 0;
    diagnostics.rx_frames = 0;
    diagnostics.rx_errors = 0;
    diagnostics.rx_invalid = 0;
    diagnostics.last_rx_length = 0;
    diagnostics.last_error_status = 0;
    diagnostics.last_tx_status = 0;
    diagnostics.last_tx_misc = 0;
    diagnostics.tx_descriptor_address = transmit_ring[net_tx_index].address;
    diagnostics.tx_descriptor_length = transmit_ring[net_tx_index].length;
    diagnostics.tx_descriptor_status = transmit_ring[net_tx_index].status;
    diagnostics.init_failure = 0;
    diagnostics.sw_csr0 = 0;
    diagnostics.dw_csr0 = 0;
    diagnostics.rx_descriptor_status = 0;
    diagnostics.rx_message_length = 0;
}

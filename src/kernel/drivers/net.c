#include "net.h"

#define PCNET_RDP 0x10
#define PCNET_RAP 0x12
#define PCNET_RESET 0x14
#define PCNET_BDP 0x16

#define CSR0_INIT 0x0001
#define CSR0_STRT 0x0002
#define CSR0_STOP 0x0004
#define CSR0_TINT 0x0200
#define CSR0_RINT 0x0400
#define CSR0_IDON 0x0100
#define CSR0_ERR  0x8000

#define BCR20_SSTYLE_32 0x0102

#define DESCRIPTOR_OWN 0x80000000U
#define DESCRIPTOR_ERR 0x40000000U
#define DESCRIPTOR_STP 0x02000000U
#define DESCRIPTOR_ENP 0x01000000U

#define RING_COUNT 8
#define BUFFER_SIZE 1536
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC
#define PCNET_VENDOR_ID 0x1022
#define PCNET_DEVICE_ID 0x2000

typedef struct {
    uint16_t mode;
    uint8_t reserved_length;
    uint8_t ring_length;
    uint8_t physical_address[6];
    uint16_t reserved;
    uint8_t logical_address[4];
    uint32_t receive_ring;
    uint32_t transmit_ring;
} __attribute__((packed, aligned(16))) pcnet_init_block_t;

typedef struct {
    uint32_t address;
    uint32_t flags;
    uint32_t flags2;
    uint32_t available;
} __attribute__((packed)) pcnet_descriptor_t;

static uint16_t net_io_base;
static uint8_t net_ready;
static uint8_t net_tx_index;
static uint8_t net_rx_index;
static uint64_t net_mac;
static uint32_t net_ip;
static net_receive_handler_t net_handler;
static net_diagnostics_t diagnostics;

static pcnet_init_block_t init_block __attribute__((aligned(16)));
static pcnet_descriptor_t receive_ring[RING_COUNT] __attribute__((aligned(16)));
static pcnet_descriptor_t transmit_ring[RING_COUNT] __attribute__((aligned(16)));
static uint8_t receive_buffers[RING_COUNT][BUFFER_SIZE] __attribute__((aligned(16)));
static uint8_t transmit_buffers[RING_COUNT][BUFFER_SIZE] __attribute__((aligned(16)));

static void pcnet_select(uint16_t register_number) {
    outw(net_io_base + PCNET_RAP, register_number);
}

static uint16_t pcnet_read(uint16_t register_number) {
    pcnet_select(register_number);
    return inw(net_io_base + PCNET_RDP);
}

static void pcnet_write(uint16_t register_number, uint16_t value) {
    pcnet_select(register_number);
    outw(net_io_base + PCNET_RDP, value);
}

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t function,
                           uint8_t offset) {
    uint32_t address = 0x80000000U
                     | ((uint32_t)bus << 16)
                     | ((uint32_t)slot << 11)
                     | ((uint32_t)function << 8)
                     | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write16(uint8_t bus, uint8_t slot, uint8_t function,
                        uint8_t offset, uint16_t value) {
    uint32_t address = 0x80000000U
                     | ((uint32_t)bus << 16)
                     | ((uint32_t)slot << 11)
                     | ((uint32_t)function << 8)
                     | (offset & 0xFC);
    uint32_t current;
    outl(PCI_CONFIG_ADDRESS, address);
    current = inl(PCI_CONFIG_DATA);
    if (offset & 2) current = (current & 0x0000FFFFU) | ((uint32_t)value << 16);
    else current = (current & 0xFFFF0000U) | value;
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, current);
}

static uint16_t find_pcnet_io_base(void) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t function = 0; function < 8; function++) {
                uint32_t identity = pci_read32((uint8_t)bus, slot, function, 0);
                uint16_t vendor = (uint16_t)identity;
                uint16_t device = (uint16_t)(identity >> 16);
                uint32_t bar;

                if (vendor != PCNET_VENDOR_ID || device != PCNET_DEVICE_ID)
                    continue;
                bar = pci_read32((uint8_t)bus, slot, function, 0x10);
                if (!(bar & 1) || (bar & 0xFFFFFFFCU) == 0)
                    return 0;
                pci_write16((uint8_t)bus, slot, function, 4, 0x0005);
                return (uint16_t)(bar & 0xFFFC);
            }
        }
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
    if (io_base == 0) return 0;

    net_io_base = io_base;
    net_ready = 0;
    net_tx_index = 0;
    net_rx_index = 0;
    net_mac = read_mac();
    net_reset_diagnostics();
    diagnostics.io_base = io_base;

    if (pcnet_read(0) == 0xFFFF) return 0;

    pcnet_write(20, BCR20_SSTYLE_32);
    pcnet_write(0, CSR0_STOP);

    init_block.mode = 0;
    init_block.reserved_length = 0;
    init_block.ring_length = 0x33; 
    init_block.reserved = 0;
    init_block.logical_address[0] = 0;
    init_block.logical_address[1] = 0;
    init_block.logical_address[2] = 0;
    init_block.logical_address[3] = 0;

    for (uint8_t i = 0; i < 6; i++) {
        init_block.physical_address[i] =
            (uint8_t)((net_mac >> (i * 8)) & 0xFF);
    }

    init_block.receive_ring = aligned_address(receive_ring);
    init_block.transmit_ring = aligned_address(transmit_ring);

    for (uint8_t i = 0; i < RING_COUNT; i++) {
        receive_ring[i].address = aligned_address(receive_buffers[i]);
        receive_ring[i].flags = DESCRIPTOR_OWN | (uint32_t)(-(int32_t)BUFFER_SIZE & 0xFFF);
        receive_ring[i].flags2 = 0;
        receive_ring[i].available = 0;

        transmit_ring[i].address = aligned_address(transmit_buffers[i]);
        transmit_ring[i].flags = (uint32_t)(-(int32_t)BUFFER_SIZE & 0xFFF);
        transmit_ring[i].flags2 = 0;
        transmit_ring[i].available = 0;
    }

    pcnet_write(1, (uint16_t)((uint32_t)&init_block & 0xFFFF));
    pcnet_write(2, (uint16_t)(((uint32_t)&init_block >> 16) & 0xFFFF));
    pcnet_write(0, CSR0_INIT);

    for (volatile uint32_t timeout = 0; timeout < 1000000; timeout++) {
        uint16_t status = pcnet_read(0);
        if (status != 0xFFFF && (status & CSR0_IDON)) {
            pcnet_write(0, CSR0_IDON | CSR0_STRT | CSR0_RINT | CSR0_TINT);
            net_ready = 1;
            diagnostics.initialized = 1;
            return 1;
        }
    }

    pcnet_write(0, CSR0_STOP);
    return 0;
}

void net_shutdown(void) {
    if (!net_ready) return;
    pcnet_write(0, CSR0_STOP);
    inw(net_io_base + PCNET_RESET);
    net_ready = 0;
}

uint8_t net_is_ready(void) {
    return net_ready;
}

uint8_t net_send(const uint8_t* data, uint32_t length) {
    pcnet_descriptor_t* descriptor;
    uint8_t* destination;

    diagnostics.tx_attempts++;
    if (!net_ready || !data || length == 0 || length > NET_MAX_FRAME_SIZE) {
        return 0;
    }

    descriptor = &transmit_ring[net_tx_index];
    if (descriptor->flags & DESCRIPTOR_OWN) {
        diagnostics.tx_busy++;
        return 0;
    }

    destination = (uint8_t*)descriptor->address;
    for (uint32_t i = 0; i < length; i++) destination[i] = data[i];

    descriptor->flags2 = 0;
    descriptor->flags = DESCRIPTOR_OWN | DESCRIPTOR_STP | DESCRIPTOR_ENP
                      | (uint32_t)(-(int32_t)length & 0xFFF);
    net_tx_index = (uint8_t)((net_tx_index + 1) % RING_COUNT);
    diagnostics.tx_submitted++;
    pcnet_write(0, CSR0_TINT);
    return 1;
}

void net_poll(void) {
    uint16_t status;
    uint8_t processed = 0;

    if (!net_ready) return;

    status = pcnet_read(0);
    diagnostics.csr0 = status;
    if (status & CSR0_ERR) {
        diagnostics.last_error_status = status;
        diagnostics.rx_errors++;
        pcnet_write(0, status);
    }

    while (!(receive_ring[net_rx_index].flags & DESCRIPTOR_OWN)
           && processed < RING_COUNT) {
        pcnet_descriptor_t* descriptor = &receive_ring[net_rx_index];
        uint32_t length = descriptor->flags2 & 0x0FFF;

        diagnostics.last_rx_length = length;
        if (!(descriptor->flags & DESCRIPTOR_ERR)
            && (descriptor->flags & (DESCRIPTOR_STP | DESCRIPTOR_ENP))
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

        descriptor->flags2 = 0;
        descriptor->flags = DESCRIPTOR_OWN
                          | (uint32_t)(-(int32_t)BUFFER_SIZE & 0xFFF);
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
    init_block.logical_address[0] = (uint8_t)(address & 0xFF);
    init_block.logical_address[1] = (uint8_t)((address >> 8) & 0xFF);
    init_block.logical_address[2] = (uint8_t)((address >> 16) & 0xFF);
    init_block.logical_address[3] = (uint8_t)((address >> 24) & 0xFF);
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
    output->csr0 = net_io_base ? pcnet_read(0) : 0;
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
    diagnostics.rx_frames = 0;
    diagnostics.rx_errors = 0;
    diagnostics.rx_invalid = 0;
    diagnostics.last_rx_length = 0;
    diagnostics.last_error_status = 0;
}
#include "nvme.h"
#include "pci.h"
#include "serial.h"
#include "delay.h"
#include "../paging.h"

#define PCI_CLASS_STORAGE 0x01
#define PCI_SUBCLASS_NVME 0x08
#define PCI_PROGIF_NVME   0x02

#define NVME_REG_CAP    0x0000
#define NVME_REG_VS     0x0008
#define NVME_REG_CC     0x0014
#define NVME_REG_CSTS   0x001C
#define NVME_REG_AQA    0x0024
#define NVME_REG_ASQ    0x0028
#define NVME_REG_ACQ    0x0030

#define NVME_CC_EN        (1u << 0)
#define NVME_CC_CSS_NVM   (0u << 4)
#define NVME_CC_MPS_4K    (0u << 7)
#define NVME_CC_IOSQES_64 (6u << 16)
#define NVME_CC_IOCQES_16 (4u << 20)

#define NVME_CSTS_RDY     (1u << 0)

#define NVME_ADMIN_CREATE_SQ   0x01
#define NVME_ADMIN_CREATE_CQ   0x05
#define NVME_ADMIN_IDENTIFY    0x06
#define NVME_NVM_WRITE         0x01
#define NVME_NVM_READ          0x02

#define ADMIN_QUEUE_SIZE 64u
#define IO_QUEUE_SIZE    64u

typedef struct {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t cid;
    uint32_t nsid;
    uint32_t reserved[2];
    uint64_t metadata;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed)) nvme_cmd_t;

typedef struct {
    uint32_t result;
    uint32_t rsvd;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;
} __attribute__((packed)) nvme_cqe_t;

static volatile uint8_t* nvme_mmio = 0;
static uint32_t dstrd = 0;
static uint32_t ns_size_lba = 0;
static uint8_t ready = 0;

static nvme_cmd_t* admin_sq = 0;
static nvme_cqe_t* admin_cq = 0;
static uint16_t admin_sq_tail = 0;
static uint16_t admin_cq_head = 0;
static uint8_t  admin_cq_phase = 1;

static nvme_cmd_t* io_sq = 0;
static nvme_cqe_t* io_cq = 0;
static uint16_t io_sq_tail = 0;
static uint16_t io_cq_head = 0;
static uint8_t  io_cq_phase = 1;

static uint8_t* dma_bounce_page = 0;

static inline uint32_t nvme_read32(uint32_t reg) {
    return *(volatile uint32_t*)(nvme_mmio + reg);
}

static inline void nvme_write32(uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(nvme_mmio + reg) = val;
}

static inline void nvme_write64(uint32_t reg, uint64_t val) {
    *(volatile uint32_t*)(nvme_mmio + reg) = (uint32_t)val;
    *(volatile uint32_t*)(nvme_mmio + reg + 4) = (uint32_t)(val >> 32);
}

static inline void ring_doorbell(uint16_t qid, uint8_t is_cq, uint16_t val) {
    uint32_t offset = 0x1000 + (2 * qid + (is_cq ? 1 : 0)) * (4 << dstrd);
    nvme_write32(offset, val);
}

static int nvme_submit_admin(nvme_cmd_t* cmd, nvme_cqe_t* cqe_out) {
    cmd->cid = admin_sq_tail;
    admin_sq[admin_sq_tail] = *cmd;
    __asm__ volatile ("" ::: "memory");

    admin_sq_tail = (admin_sq_tail + 1) % ADMIN_QUEUE_SIZE;
    ring_doorbell(0, 0, admin_sq_tail);

    uint32_t timeout = 200000;
    while (timeout--) {
        volatile nvme_cqe_t* cqe = &admin_cq[admin_cq_head];
        __asm__ volatile ("" ::: "memory");
        if ((cqe->status & 1) == admin_cq_phase) {
            if (cqe_out) *cqe_out = *cqe;
            admin_cq_head = (admin_cq_head + 1) % ADMIN_QUEUE_SIZE;
            if (admin_cq_head == 0) admin_cq_phase ^= 1;
            ring_doorbell(0, 1, admin_cq_head);
            return ((cqe->status >> 1) & 0x7FFF) == 0 ? 0 : -1;
        }
        driver_delay_us(5);
    }
    return -1;
}

static int nvme_submit_io(nvme_cmd_t* cmd, nvme_cqe_t* cqe_out) {
    cmd->cid = io_sq_tail;
    io_sq[io_sq_tail] = *cmd;
    __asm__ volatile ("" ::: "memory");

    io_sq_tail = (io_sq_tail + 1) % IO_QUEUE_SIZE;
    ring_doorbell(1, 0, io_sq_tail);

    uint32_t timeout = 500000;
    while (timeout--) {
        volatile nvme_cqe_t* cqe = &io_cq[io_cq_head];
        __asm__ volatile ("" ::: "memory");
        if ((cqe->status & 1) == io_cq_phase) {
            if (cqe_out) *cqe_out = *cqe;
            io_cq_head = (io_cq_head + 1) % IO_QUEUE_SIZE;
            if (io_cq_head == 0) io_cq_phase ^= 1;
            ring_doorbell(1, 1, io_cq_head);
            return ((cqe->status >> 1) & 0x7FFF) == 0 ? 0 : -1;
        }
        driver_delay_us(2);
    }
    return -1;
}

int nvme_init(void) {
    ready = 0;
    const pci_device_t* dev = 0;

    for (uint32_t i = 0; i < pci_device_count(); i++) {
        const pci_device_t* p = pci_device_at(i);
        if (p->class_code == PCI_CLASS_STORAGE && p->subclass == PCI_SUBCLASS_NVME) {
            dev = p;
            break;
        }
    }
    if (!dev) {
        debug_print("[NVMe] No NVMe controller found on PCI bus.\n");
        return -1;
    }

    if (!pci_power_on(dev) || !pci_enable_device(dev, PCI_COMMAND_MEMORY)) {
        return -1;
    }
    pci_set_master(dev, 1);

    pci_bar_t bar0;
    if (!pci_get_bar(dev, 0, &bar0) || bar0.io || !bar0.address) {
        return -1;
    }
    nvme_mmio = (volatile uint8_t*)(uint32_t)bar0.address;

    uint32_t cap_lo = nvme_read32(NVME_REG_CAP);
    dstrd = (cap_lo >> 12) & 0x0F;

    uint32_t cc = nvme_read32(NVME_REG_CC);
    if (cc & NVME_CC_EN) {
        nvme_write32(NVME_REG_CC, cc & ~NVME_CC_EN);
    }
    while (nvme_read32(NVME_REG_CSTS) & NVME_CSTS_RDY) {
        driver_delay_us(100);
    }

    admin_sq = (nvme_cmd_t*)paging_alloc_contiguous(1);
    admin_cq = (nvme_cqe_t*)paging_alloc_contiguous(1);
    io_sq    = (nvme_cmd_t*)paging_alloc_contiguous(1);
    io_cq    = (nvme_cqe_t*)paging_alloc_contiguous(1);
    dma_bounce_page = (uint8_t*)paging_alloc_contiguous(1);

    if (!admin_sq || !admin_cq || !io_sq || !io_cq || !dma_bounce_page) {
        debug_print("[NVMe] Failed to allocate queue buffers.\n");
        return -1;
    }

    for (unsigned i = 0; i < PAGE_SIZE; i++) {
        ((uint8_t*)admin_sq)[i] = 0;
        ((uint8_t*)admin_cq)[i] = 0;
        ((uint8_t*)io_sq)[i] = 0;
        ((uint8_t*)io_cq)[i] = 0;
    }

    admin_sq_tail = admin_cq_head = 0;
    admin_cq_phase = 1;

    nvme_write32(NVME_REG_AQA, ((ADMIN_QUEUE_SIZE - 1) << 16) | (ADMIN_QUEUE_SIZE - 1));
    nvme_write64(NVME_REG_ASQ, (uint32_t)admin_sq);
    nvme_write64(NVME_REG_ACQ, (uint32_t)admin_cq);

    cc = NVME_CC_EN | NVME_CC_CSS_NVM | NVME_CC_MPS_4K |
         NVME_CC_IOSQES_64 | NVME_CC_IOCQES_16;
    nvme_write32(NVME_REG_CC, cc);

    while (!(nvme_read32(NVME_REG_CSTS) & NVME_CSTS_RDY)) {
        driver_delay_us(100);
    }

    nvme_cmd_t cmd = {0};
    cmd.opcode = NVME_ADMIN_CREATE_CQ;
    cmd.prp1 = (uint32_t)io_cq;
    cmd.cdw10 = ((IO_QUEUE_SIZE - 1) << 16) | 1;
    cmd.cdw11 = 1;
    if (nvme_submit_admin(&cmd, 0) < 0) return -1;

    cmd = (nvme_cmd_t){0};
    cmd.opcode = NVME_ADMIN_CREATE_SQ;
    cmd.prp1 = (uint32_t)io_sq;
    cmd.cdw10 = ((IO_QUEUE_SIZE - 1) << 16) | 1;
    cmd.cdw11 = (1 << 16) | 1;
    if (nvme_submit_admin(&cmd, 0) < 0) return -1;

    io_sq_tail = io_cq_head = 0;
    io_cq_phase = 1;

    cmd = (nvme_cmd_t){0};
    cmd.opcode = NVME_ADMIN_IDENTIFY;
    cmd.nsid = 1;
    cmd.prp1 = (uint32_t)dma_bounce_page;
    cmd.cdw10 = 0;

    if (nvme_submit_admin(&cmd, 0) == 0) {
        ns_size_lba = *(uint32_t*)dma_bounce_page;
        debug_print("[NVMe] Initialized successfully. Sectors: ");
        debug_hex(ns_size_lba);
        debug_print("\n");
        ready = 1;
    }

    return ready ? 0 : -1;
}

int nvme_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer) {
    if (!ready || !count || !buffer) return 0;

    while (count > 0) {
        uint8_t chunk = count > 8 ? 8 : count;

        nvme_cmd_t cmd = {0};
        cmd.opcode = NVME_NVM_READ;
        cmd.nsid = 1;
        cmd.prp1 = (uint32_t)dma_bounce_page;
        cmd.cdw10 = lba;
        cmd.cdw11 = 0;
        cmd.cdw12 = (uint32_t)(chunk - 1);

        if (nvme_submit_io(&cmd, 0) < 0) return 0;

        uint32_t bytes = (uint32_t)chunk * 512;
        for (uint32_t i = 0; i < bytes; i++) {
            buffer[i] = dma_bounce_page[i];
        }

        buffer += bytes;
        lba += chunk;
        count -= chunk;
    }
    return 1;
}

int nvme_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer) {
    if (!ready || !count || !buffer) return 0;

    while (count > 0) {
        uint8_t chunk = count > 8 ? 8 : count;
        uint32_t bytes = (uint32_t)chunk * 512;

        for (uint32_t i = 0; i < bytes; i++) {
            dma_bounce_page[i] = buffer[i];
        }

        nvme_cmd_t cmd = {0};
        cmd.opcode = NVME_NVM_WRITE;
        cmd.nsid = 1;
        cmd.prp1 = (uint32_t)dma_bounce_page;
        cmd.cdw10 = lba;
        cmd.cdw11 = 0;
        cmd.cdw12 = (uint32_t)(chunk - 1);

        if (nvme_submit_io(&cmd, 0) < 0) return 0;

        buffer += bytes;
        lba += chunk;
        count -= chunk;
    }
    return 1;
}

int nvme_is_ready(void) { return ready; }
uint32_t nvme_get_sector_count(void) { return ns_size_lba; }
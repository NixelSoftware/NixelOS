#include "virtio_blk.h"

#define MMIO_BASE   0x0A000000u
#define MMIO_STRIDE 0x200u
#define MMIO_SLOTS  32u

#define REG_MAGIC        0x000u
#define REG_VERSION      0x004u
#define REG_DEVICE_ID    0x008u
#define REG_DEV_FEAT     0x010u
#define REG_DEV_FEAT_SEL 0x014u
#define REG_DRV_FEAT     0x020u
#define REG_DRV_FEAT_SEL 0x024u
#define REG_QUEUE_SEL    0x030u
#define REG_QUEUE_MAX    0x034u
#define REG_QUEUE_NUM    0x038u
#define REG_QUEUE_READY  0x044u
#define REG_QUEUE_NOTIFY 0x050u
#define REG_STATUS       0x070u
#define REG_DESC_LOW     0x080u
#define REG_DESC_HIGH    0x084u
#define REG_AVAIL_LOW    0x090u
#define REG_AVAIL_HIGH   0x094u
#define REG_USED_LOW     0x0A0u
#define REG_USED_HIGH    0x0A4u
#define REG_CONFIG       0x100u

#define STATUS_ACK          1u
#define STATUS_DRIVER       2u
#define STATUS_DRIVER_OK    4u
#define STATUS_FEATURES_OK  8u

#define DESC_F_NEXT  1u
#define DESC_F_WRITE 2u

#define BLK_DEVICE_ID 2u
#define VRING_NUM     8u
#define SECTOR_SIZE   512u

#define BLK_T_IN  0u
#define BLK_T_OUT 1u

struct blk_request {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VRING_NUM];
    uint16_t used_event;
} __attribute__((aligned(2)));

struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct { uint32_t id, len; } ring[VRING_NUM];
    uint16_t avail_event;
} __attribute__((aligned(4)));

static uint32_t mmio_base;
static uint16_t last_used;
static uint64_t sector_count;
static int ready;

static struct vring_desc desc[VRING_NUM] __attribute__((aligned(16)));
static struct vring_avail avail __attribute__((aligned(4096)));
static struct vring_used used __attribute__((aligned(4096)));
static struct blk_request request;
static uint8_t status_byte;

static inline void wr(uint32_t reg, uint32_t value) {
    *(volatile uint32_t*)(mmio_base + reg) = value;
}

static inline uint32_t rd(uint32_t reg) {
    return *(volatile uint32_t*)(mmio_base + reg);
}

static inline void dmb(void) { __asm__ volatile ("dmb" : : : "memory"); }

static int find_device(void) {
    for (uint32_t i = 0; i < MMIO_SLOTS; i++) {
        uint32_t base = MMIO_BASE + i * MMIO_STRIDE;
        if (*(volatile uint32_t*)(base + REG_MAGIC) != 0x74726976u)
            continue;
        if (*(volatile uint32_t*)(base + REG_VERSION) != 2u)
            continue;
        if (*(volatile uint32_t*)(base + REG_DEVICE_ID) != BLK_DEVICE_ID)
            continue;
        mmio_base = base;
        return 1;
    }
    return 0;
}

static int submit(uint32_t type, uint64_t sector, void* buffer, uint32_t length) {
    request.type = type;
    request.reserved = 0;
    request.sector = sector;
    status_byte = 0xFFu;

    desc[0].addr = (uint32_t)&request;
    desc[0].len = sizeof(request);
    desc[0].flags = DESC_F_NEXT;
    desc[0].next = 1;

    desc[1].addr = (uint32_t)buffer;
    desc[1].len = length;
    desc[1].flags = DESC_F_NEXT | (type == BLK_T_IN ? DESC_F_WRITE : 0u);
    desc[1].next = 2;

    desc[2].addr = (uint32_t)&status_byte;
    desc[2].len = 1;
    desc[2].flags = DESC_F_WRITE;
    desc[2].next = 0;

    uint16_t slot = avail.idx % VRING_NUM;
    avail.ring[slot] = 0;
    dmb();
    avail.idx++;
    dmb();
    wr(REG_QUEUE_NOTIFY, 0);

    while (used.idx == last_used)
        dmb();
    last_used++;
    return status_byte == 0 ? 0 : -1;
}

int virtio_blk_init(void) {
    if (!find_device()) return 0;

    wr(REG_STATUS, 0);
    wr(REG_STATUS, STATUS_ACK);
    wr(REG_STATUS, STATUS_ACK | STATUS_DRIVER);

    wr(REG_DEV_FEAT_SEL, 1);
    (void)rd(REG_DEV_FEAT);
    wr(REG_DRV_FEAT_SEL, 1);
    wr(REG_DRV_FEAT, 1u);
    wr(REG_STATUS, STATUS_ACK | STATUS_DRIVER | STATUS_FEATURES_OK);
    if (!(rd(REG_STATUS) & STATUS_FEATURES_OK))
        return 0;

    wr(REG_QUEUE_SEL, 0);
    if (!rd(REG_QUEUE_MAX)) return 0;
    wr(REG_QUEUE_NUM, VRING_NUM);
    wr(REG_DESC_LOW, (uint32_t)desc);
    wr(REG_DESC_HIGH, 0);
    wr(REG_AVAIL_LOW, (uint32_t)&avail);
    wr(REG_AVAIL_HIGH, 0);
    wr(REG_USED_LOW, (uint32_t)&used);
    wr(REG_USED_HIGH, 0);
    wr(REG_QUEUE_READY, 1);
    wr(REG_STATUS, STATUS_ACK | STATUS_DRIVER | STATUS_FEATURES_OK | STATUS_DRIVER_OK);

    uint32_t low = rd(REG_CONFIG);
    uint32_t high = rd(REG_CONFIG + 4);
    sector_count = ((uint64_t)high << 32) | low;

    ready = 1;
    return 1;
}

int virtio_blk_read(uint64_t sector, void* buffer, uint32_t count) {
    if (!ready || !buffer || !count) return -1;
    return submit(BLK_T_IN, sector, buffer, count * SECTOR_SIZE);
}

int virtio_blk_write(uint64_t sector, const void* buffer, uint32_t count) {
    if (!ready || !buffer || !count) return -1;
    return submit(BLK_T_OUT, sector, (void*)buffer, count * SECTOR_SIZE);
}

int virtio_blk_ready(void) {
    return ready;
}

uint64_t virtio_blk_sectors(void) {
    return sector_count;
}

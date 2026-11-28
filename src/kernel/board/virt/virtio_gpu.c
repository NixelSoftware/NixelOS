#include "virtio_gpu.h"
#include "serial.h"
#include "arch/arch.h"

#define MMIO_BASE   0x0A000000u
#define MMIO_STRIDE 0x200u
#define MMIO_SLOTS  32u

#define VIRTIO_MMIO_MAGIC           0x000u
#define VIRTIO_MMIO_VERSION         0x004u
#define VIRTIO_MMIO_DEVICE_ID       0x008u
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010u
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014u
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020u
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024u
#define VIRTIO_MMIO_QUEUE_SEL       0x030u
#define VIRTIO_MMIO_QUEUE_NUM_MAX   0x034u
#define VIRTIO_MMIO_QUEUE_NUM       0x038u
#define VIRTIO_MMIO_QUEUE_READY     0x044u
#define VIRTIO_MMIO_QUEUE_NOTIFY    0x050u
#define VIRTIO_MMIO_STATUS          0x070u
#define VIRTIO_MMIO_DESC_LOW        0x080u
#define VIRTIO_MMIO_DESC_HIGH       0x084u
#define VIRTIO_MMIO_AVAIL_LOW       0x090u
#define VIRTIO_MMIO_AVAIL_HIGH      0x094u
#define VIRTIO_MMIO_USED_LOW        0x0A0u
#define VIRTIO_MMIO_USED_HIGH       0x0A4u

#define VIRTIO_STATUS_ACK           1u
#define VIRTIO_STATUS_DRIVER        2u
#define VIRTIO_STATUS_DRIVER_OK     4u
#define VIRTIO_STATUS_FEATURES_OK   8u

#define VIRTQ_DESC_F_NEXT  1u
#define VIRTQ_DESC_F_WRITE 2u

#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D      0x0101u
#define VIRTIO_GPU_CMD_SET_SCANOUT             0x0103u
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH          0x0104u
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D     0x0105u
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106u
#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM       2u

#define VRING_NUM 8u

struct vgpu_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed));

struct vgpu_rect {
    uint32_t x, y, width, height;
} __attribute__((packed));

struct vgpu_create2d {
    struct vgpu_hdr h;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct vgpu_attach_entry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed));

struct vgpu_attach {
    struct vgpu_hdr h;
    uint32_t resource_id;
    uint32_t nr_entries;
    struct vgpu_attach_entry entry;
} __attribute__((packed));

struct vgpu_scanout {
    struct vgpu_hdr h;
    struct vgpu_rect r;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed));

struct vgpu_transfer {
    struct vgpu_hdr h;
    struct vgpu_rect r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

struct vgpu_flush {
    struct vgpu_hdr h;
    struct vgpu_rect r;
    uint32_t resource_id;
    uint32_t padding;
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
static uint32_t width, height;
static uint32_t* framebuffer;
static uint16_t last_used;

static struct vring_desc desc[VRING_NUM] __attribute__((aligned(16)));
static struct vring_avail avail __attribute__((aligned(4096)));
static struct vring_used used __attribute__((aligned(4096)));

static uint32_t fb_storage[640u * 480u] __attribute__((aligned(4096)));

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
        if (*(volatile uint32_t*)(base + VIRTIO_MMIO_MAGIC) != 0x74726976u)
            continue;
        if (*(volatile uint32_t*)(base + VIRTIO_MMIO_VERSION) != 2u)
            continue;
        if (*(volatile uint32_t*)(base + VIRTIO_MMIO_DEVICE_ID) != 16u)
            continue;
        mmio_base = base;
        return 1;
    }
    return 0;
}

static int setup_device(void) {
    wr(VIRTIO_MMIO_STATUS, 0);
    wr(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACK);
    wr(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    wr(VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
    (void)rd(VIRTIO_MMIO_DEVICE_FEATURES);
    wr(VIRTIO_MMIO_DEVICE_FEATURES_SEL, 1);
    (void)rd(VIRTIO_MMIO_DEVICE_FEATURES);

    wr(VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
    wr(VIRTIO_MMIO_DRIVER_FEATURES, 0);
    wr(VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1);
    wr(VIRTIO_MMIO_DRIVER_FEATURES, 1u);
    wr(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);
    if (!(rd(VIRTIO_MMIO_STATUS) & VIRTIO_STATUS_FEATURES_OK))
        return 0;

    wr(VIRTIO_MMIO_QUEUE_SEL, 0);
    uint32_t max = rd(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (!max) return 0;
    wr(VIRTIO_MMIO_QUEUE_NUM, VRING_NUM);
    wr(VIRTIO_MMIO_DESC_LOW, (uint32_t)desc);
    wr(VIRTIO_MMIO_DESC_HIGH, 0);
    wr(VIRTIO_MMIO_AVAIL_LOW, (uint32_t)&avail);
    wr(VIRTIO_MMIO_AVAIL_HIGH, 0);
    wr(VIRTIO_MMIO_USED_LOW, (uint32_t)&used);
    wr(VIRTIO_MMIO_USED_HIGH, 0);
    wr(VIRTIO_MMIO_QUEUE_READY, 1);
    wr(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                          VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);
    return 1;
}

static uint32_t submit(void* cmd, uint32_t len) {
    uint32_t irq = arch_irq_save();
    desc[0].addr = (uint32_t)cmd;
    desc[0].len = len;
    desc[0].flags = 0;
    desc[0].next = 0;

    uint16_t slot = avail.idx % VRING_NUM;
    avail.ring[slot] = 0;
    dmb();
    avail.idx++;
    dmb();
    wr(VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    while (used.idx == last_used)
        dmb();
    dmb();
    uint32_t used_len = used.ring[last_used % VRING_NUM].len;
    last_used++;
    arch_irq_restore(irq);
    return used_len;
}

int virtio_gpu_init(uint32_t w, uint32_t h) {
    if (w > 640u || h > 480u) return 0;
    width = w;
    height = h;
    framebuffer = fb_storage;

    if (!find_device()) return 0;
    if (!setup_device()) return 0;

    static struct vgpu_create2d create;
    create.h.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    create.resource_id = 1;
    create.format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
    create.width = width;
    create.height = height;
    submit(&create, sizeof(create));

    static struct vgpu_attach attach;
    attach.h.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    attach.resource_id = 1;
    attach.nr_entries = 1;
    attach.entry.addr = (uint32_t)framebuffer;
    attach.entry.length = width * height * 4u;
    attach.entry.padding = 0;
    submit(&attach, sizeof(attach));

    static struct vgpu_scanout scanout;
    scanout.h.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    scanout.r.x = 0;
    scanout.r.y = 0;
    scanout.r.width = width;
    scanout.r.height = height;
    scanout.scanout_id = 0;
    scanout.resource_id = 1;
    submit(&scanout, sizeof(scanout));

    return 1;
}

void virtio_gpu_present(void) {
    static struct vgpu_transfer transfer;
    transfer.h.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    transfer.r.x = 0;
    transfer.r.y = 0;
    transfer.r.width = width;
    transfer.r.height = height;
    transfer.offset = 0;
    transfer.resource_id = 1;
    transfer.padding = 0;
    submit(&transfer, sizeof(transfer));

    static struct vgpu_flush flush;
    flush.h.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    flush.r.x = 0;
    flush.r.y = 0;
    flush.r.width = width;
    flush.r.height = height;
    flush.resource_id = 1;
    flush.padding = 0;
    submit(&flush, sizeof(flush));
}

uint32_t* virtio_gpu_framebuffer(void) {
    return framebuffer;
}

uint32_t virtio_gpu_width(void) {
    return width;
}

uint32_t virtio_gpu_height(void) {
    return height;
}

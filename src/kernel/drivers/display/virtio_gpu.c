#include "video_drivers.h"
#include "../pci.h"
#include "../delay.h"
#include "../serial.h"
#include "../../paging.h"

/* Modern virtio PCI transport, split control queue, unaccelerated 2D scanout.
 * Wire layouts: virtio 1.2 sections 4.1 and 5.7. All DMA addresses are physical
 * addresses in the kernel's identity map. Only the display worker submits once
 * asynchronous presentation starts; resize and scene changes hold kernel_mutex.
 */
#define QUEUE_SIZE 8u
#define BUFFER_PAGES ((SCREEN_MAX_WIDTH * SCREEN_MAX_HEIGHT * 4u + PAGE_SIZE - 1u) / PAGE_SIZE)
#define GPU_DISPLAY_INFO 0x100u
#define GPU_CREATE 0x101u
#define GPU_UNREF 0x102u
#define GPU_SCANOUT 0x103u
#define GPU_FLUSH 0x104u
#define GPU_TRANSFER 0x105u
#define GPU_ATTACH 0x106u
#define GPU_OK 0x1100u

typedef struct { uint64_t address; uint32_t length; uint16_t flags, next; } descriptor_t;
typedef struct { uint32_t type, flags; uint64_t fence; uint32_t context, padding; } gpu_header_t;
typedef struct { uint32_t x, y, width, height; } gpu_rect_t;
static struct {
    descriptor_t desc[QUEUE_SIZE];
    volatile uint16_t avail_flags, avail_index, avail_ring[QUEUE_SIZE], avail_event;
    volatile uint16_t used_flags __attribute__((aligned(4)));
    volatile uint16_t used_index;
    struct { uint32_t id, length; } used_ring[QUEUE_SIZE];
    uint16_t used_event;
} __attribute__((aligned(4096))) queue;
static uint8_t request[128] __attribute__((aligned(16)));
static struct {
    gpu_header_t header;
    struct { gpu_rect_t rect; uint32_t enabled, flags; } modes[16];
} response __attribute__((aligned(16)));
static volatile uint8_t *common, *device_config;
static volatile uint16_t* notify;
static uint32_t* backing;
static uint32_t resource;
static uint16_t consumed;
static int active, dirty;
static uint32_t dirty_x, dirty_y, dirty_right, dirty_bottom;
static const pci_device_t* gpu_device;

static void barrier(void) { __asm__ volatile ("lock; addl $0, 0(%%esp)" : : : "memory", "cc"); }
static uint16_t read16(unsigned offset) { return *(volatile uint16_t*)(common + offset); }
static uint32_t read32(unsigned offset) { return *(volatile uint32_t*)(common + offset); }
static void write16(unsigned offset, uint16_t value) { *(volatile uint16_t*)(common + offset) = value; }
static void write32(unsigned offset, uint32_t value) { *(volatile uint32_t*)(common + offset) = value; }
static void write_address(unsigned offset, const void* ptr) {
    write32(offset, (uint32_t)ptr); write32(offset + 4, 0);
}
static void fail(void) {
    active = 0;
    /* Keep DMA storage allocated even on failure. Never recycle outstanding DMA. */
    common[20] = 0;
    barrier();
    debug_print("[VIDEO] virtio-gpu command failed; display stopped\n");
}
static void command_init(uint32_t type) {
    for (unsigned i = 0; i < sizeof(request); i++) request[i] = 0;
    gpu_header_t* header = (gpu_header_t*)request;
    header->type = type;
    header->flags = 1; /* fence: response signals completion, including transfers */
}
static int submit(uint32_t size, uint32_t expected) {
    if (!active) return 0;
    response.header.type = 0;
    queue.desc[0] = (descriptor_t){(uint32_t)request, size, 1, 1};
    queue.desc[1] = (descriptor_t){(uint32_t)&response, sizeof(response), 2, 0};
    queue.avail_ring[queue.avail_index % QUEUE_SIZE] = 0;
    barrier();
    queue.avail_index++;
    barrier();
    *notify = 0;
    for (unsigned wait = 0; wait < 10000; wait++) {
        if (queue.used_index != consumed) {
            barrier();
            unsigned index = consumed % QUEUE_SIZE;
            if (queue.used_ring[index].id != 0 ||
                queue.used_ring[index].length < sizeof(gpu_header_t)) break;
            consumed++;
            if (response.header.type == expected) return 1;
            break;
        }
        driver_delay_us(10);
    }
    fail();
    return 0;
}
static int get_size(uint32_t* width, uint32_t* height) {
    command_init(GPU_DISPLAY_INFO);
    if (!submit(sizeof(gpu_header_t), GPU_OK + 1)) return 0;
    if (!response.modes[0].enabled) return 0;
    *width = response.modes[0].rect.width;
    *height = response.modes[0].rect.height;
    /* Bound allocation and keep legacy desktop controls usable. */
    if (*width < 640) *width = 640;
    if (*height < 480) *height = 480;
    if (*width > SCREEN_MAX_WIDTH) *width = SCREEN_MAX_WIDTH;
    if (*height > SCREEN_MAX_HEIGHT) *height = SCREEN_MAX_HEIGHT;
    return 1;
}
static int set_mode(video_framebuffer_t* fb, uint32_t width, uint32_t height) {
    uint32_t old = resource, next = old == 1 ? 2 : 1;
    command_init(GPU_CREATE);
    uint32_t* args = (uint32_t*)(request + sizeof(gpu_header_t));
    args[0] = next; args[1] = 2; /* B8G8R8X8_UNORM */
    args[2] = width; args[3] = height;
    if (!submit(40, GPU_OK)) return 0;
    command_init(GPU_ATTACH);
    args[0] = next; args[1] = 1; /* single contiguous backing entry */
    args[2] = (uint32_t)backing; args[3] = 0;
    args[4] = width * height * 4u; args[5] = 0;
    if (!submit(48, GPU_OK)) return 0;
    command_init(GPU_SCANOUT);
    args[2] = width; args[3] = height; args[4] = 0; args[5] = next;
    if (!submit(48, GPU_OK)) return 0;
    resource = next;
    if (old) {
        command_init(GPU_UNREF); args[0] = old;
        if (!submit(32, GPU_OK)) return 0;
    }
    screen_width = width; screen_height = height;
    *fb = (video_framebuffer_t){(uint32_t)backing, 32, width * 4u, "QEMU virtio-gpu"};
    dirty = 1;
    dirty_x = dirty_y = 0;
    dirty_right = width; dirty_bottom = height;
    debug_print("[VIDEO] virtio-gpu mode changed\n");
    return 1;
}

/* Validate capability regions against firmware-assigned BAR sizes before DMA. */
int virtio_gpu_init(video_framebuffer_t* fb) {
    pci_init();
    uint32_t cursor = 0;
    gpu_device = pci_find_device(0x1AF4, 0x1050, &cursor);
    if (!gpu_device) return 0;
    pci_bar_t bars[6];
    uint8_t valid[6] = {0};
    for (unsigned i = 0; i < 6; i++) {
        valid[i] = pci_probe_bar(gpu_device, i, &bars[i]) && !bars[i].io &&
            bars[i].address && bars[i].address <= 0xFFFFFFFFu &&
            bars[i].size <= 0xFFFFFFFFu - bars[i].address;
        if (valid[i] && bars[i].width64) i++;
    }
    volatile uint8_t* notify_base = 0;
    uint32_t notify_length = 0, notify_multiplier = 0;
    uint8_t cap = pci_read8(gpu_device, 0x34) & ~3u;
    for (unsigned count = 0; cap >= 0x40 && cap <= 0xEC && count < 48; count++) {
        uint8_t length = pci_read8(gpu_device, cap + 2);
        if (pci_read8(gpu_device, cap) == 9 && length >= 16 && cap + length <= 256) {
            uint8_t bar = pci_read8(gpu_device, cap + 4);
            uint8_t type = pci_read8(gpu_device, cap + 3);
            uint32_t offset = pci_read32(gpu_device, cap + 8);
            uint32_t size = pci_read32(gpu_device, cap + 12);
            if (bar < 6 && valid[bar] && offset <= bars[bar].size &&
                size <= bars[bar].size - offset) {
                volatile uint8_t* base = (volatile uint8_t*)((uint32_t)bars[bar].address + offset);
                if (type == 1 && size >= 56 && !((uint32_t)base & 3)) common = base;
                if (type == 4 && size >= 16 && !((uint32_t)base & 3)) device_config = base;
                if (type == 2 && length >= 20) {
                    notify_base = base; notify_length = size;
                    notify_multiplier = pci_read32(gpu_device, cap + 16);
                }
            }
        }
        cap = pci_read8(gpu_device, cap + 1) & ~3u;
    }
    if (!common || !device_config || !notify_base || notify_length < 2) return 0;
    if (!pci_enable_device(gpu_device, PCI_COMMAND_MEMORY)) return 0;
    pci_prepare_polling(gpu_device);
    common[20] = 0;
    barrier();
    if (common[20]) return 0;
    common[20] = 3; /* ACKNOWLEDGE | DRIVER */
    write32(0, 1);
    if (!(read32(4) & 1)) return 0; /* VIRTIO_F_VERSION_1 */
    write32(8, 0); write32(12, 0);
    write32(8, 1); write32(12, 1);
    common[20] = 11; /* FEATURES_OK */
    if (!(common[20] & 8)) return 0;
    write16(22, 0);
    if (read16(24) < QUEUE_SIZE) { common[20] = 0; return 0; }
    uint64_t notify_offset = (uint64_t)read16(30) * notify_multiplier;
    if (notify_offset > notify_length - 2 || (notify_offset & 1)) { common[20] = 0; return 0; }
    notify = (volatile uint16_t*)(notify_base + (uint32_t)notify_offset);
    backing = (uint32_t*)paging_alloc_contiguous(BUFFER_PAGES);
    if (!backing) { common[20] = 0; return 0; }
    queue.avail_flags = 1; /* polled completion, no queue interrupts */
    write16(24, QUEUE_SIZE);
    write16(26, 0xFFFF);
    write_address(32, queue.desc);
    write_address(40, (const void*)&queue.avail_flags);
    write_address(48, (const void*)&queue.used_flags);
    write16(28, 1);
    pci_set_master(gpu_device, 1);
    common[20] = 15; /* DRIVER_OK */
    active = 1;
    /* Start at the boot geometry. Process UI hints after the desktop starts,
     * avoiding splash/login assumptions and boot-time window feedback. */
    return set_mode(fb, SCREEN_DEFAULT_WIDTH, SCREEN_DEFAULT_HEIGHT);
}

int virtio_gpu_poll_resize(video_framebuffer_t* fb) {
    if (!active || !(*(volatile uint32_t*)device_config & 1)) return 0;
    /* Acknowledge before the query so a concurrent host change stays pending. */
    *(volatile uint32_t*)(device_config + 4) = 1;
    barrier();
    uint32_t width, height;
    if (!get_size(&width, &height) ||
        (width == (uint32_t)SCREEN_WIDTH && height == (uint32_t)SCREEN_HEIGHT)) return 0;
    return set_mode(fb, width, height);
}
void virtio_gpu_mark_dirty(int32_t x, int32_t y, uint32_t count) {
    if (!dirty) {
        dirty_x = x; dirty_y = y; dirty_right = x + count; dirty_bottom = y + 1;
    } else {
        if ((uint32_t)x < dirty_x) dirty_x = x;
        if ((uint32_t)y < dirty_y) dirty_y = y;
        if (x + count > dirty_right) dirty_right = x + count;
        if ((uint32_t)y + 1 > dirty_bottom) dirty_bottom = y + 1;
    }
    dirty = 1;
}
void virtio_gpu_present(void) {
    if (!active || !dirty) return;
    command_init(GPU_TRANSFER);
    uint32_t* args = (uint32_t*)(request + sizeof(gpu_header_t));
    args[0] = dirty_x; args[1] = dirty_y;
    args[2] = dirty_right - dirty_x; args[3] = dirty_bottom - dirty_y;
    args[4] = (dirty_y * SCREEN_WIDTH + dirty_x) * 4u;
    args[6] = resource; /* rect, offset u64, resource, padding */
    if (!submit(56, GPU_OK)) return;
    command_init(GPU_FLUSH);
    args[0] = dirty_x; args[1] = dirty_y;
    args[2] = dirty_right - dirty_x; args[3] = dirty_bottom - dirty_y;
    args[4] = resource;
    if (submit(48, GPU_OK)) dirty = 0;
}

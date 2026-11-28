#include "virtio_input.h"

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

#define STATUS_ACK          1u
#define STATUS_DRIVER       2u
#define STATUS_DRIVER_OK    4u
#define STATUS_FEATURES_OK  8u

#define DESC_F_WRITE 2u

#define INPUT_DEVICE_ID 18u
#define VRING_NUM       16u

#define EV_KEY 1u
#define KEY_LEFTSHIFT  42u
#define KEY_RIGHTSHIFT 54u

struct input_event {
    uint16_t type;
    uint16_t code;
    uint16_t value;
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
static int shifted;
static int ready;

static struct vring_desc desc[VRING_NUM] __attribute__((aligned(16)));
static struct vring_avail avail __attribute__((aligned(4096)));
static struct vring_used used __attribute__((aligned(4096)));
static struct input_event events_buf[VRING_NUM] __attribute__((aligned(8)));

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
        if (*(volatile uint32_t*)(base + REG_DEVICE_ID) != INPUT_DEVICE_ID)
            continue;
        mmio_base = base;
        return 1;
    }
    return 0;
}

static char translate(uint16_t code, int shift) {
    static const char letters_low[] = "asdfghjkl";
    static const char letters_home[] = "qwertyuiop";
    static const char letters_bottom[] = "zxcvbnm";

    if (code >= 30u && code <= 38u)
        return shift ? (char)(letters_low[code - 30u] - 32) : letters_low[code - 30u];
    if (code >= 16u && code <= 25u)
        return shift ? (char)(letters_home[code - 16u] - 32) : letters_home[code - 16u];
    if (code >= 44u && code <= 50u)
        return shift ? (char)(letters_bottom[code - 44u] - 32) : letters_bottom[code - 44u];

    switch (code) {
        case 2u:  return shift ? '!' : '1';
        case 3u:  return shift ? '@' : '2';
        case 4u:  return shift ? '#' : '3';
        case 5u:  return shift ? '$' : '4';
        case 6u:  return shift ? '%' : '5';
        case 7u:  return shift ? '^' : '6';
        case 8u:  return shift ? '&' : '7';
        case 9u:  return shift ? '*' : '8';
        case 10u: return shift ? '(' : '9';
        case 11u: return shift ? ')' : '0';
        case 12u: return shift ? '_' : '-';
        case 13u: return shift ? '+' : '=';
        case 14u: return '\b';
        case 15u: return '\t';
        case 26u: return shift ? '{' : '[';
        case 27u: return shift ? '}' : ']';
        case 28u: return '\n';
        case 39u: return shift ? ':' : ';';
        case 40u: return shift ? '"' : '\'';
        case 41u: return shift ? '~' : '`';
        case 43u: return shift ? '|' : '\\';
        case 51u: return shift ? '<' : ',';
        case 52u: return shift ? '>' : '.';
        case 53u: return shift ? '?' : '/';
        case 57u: return ' ';
        default:  return 0;
    }
}

int virtio_input_init(void) {
    if (!find_device()) return 0;

    wr(REG_STATUS, 0);
    wr(REG_STATUS, STATUS_ACK);
    wr(REG_STATUS, STATUS_ACK | STATUS_DRIVER);

    wr(REG_DEV_FEAT_SEL, 1);
    (void)rd(REG_DEV_FEAT);
    wr(REG_DRV_FEAT_SEL, 1);
    wr(REG_DRV_FEAT, 1u);
    wr(REG_STATUS, STATUS_ACK | STATUS_DRIVER | STATUS_FEATURES_OK);

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

    for (uint32_t i = 0; i < VRING_NUM; i++) {
        desc[i].addr = (uint32_t)&events_buf[i];
        desc[i].len = sizeof(struct input_event);
        desc[i].flags = DESC_F_WRITE;
        desc[i].next = 0;
        avail.ring[i] = (uint16_t)i;
    }
    last_used = 0;
    dmb();
    avail.idx = VRING_NUM;
    dmb();
    wr(REG_QUEUE_NOTIFY, 0);
    ready = 1;
    return 1;
}

int virtio_input_poll_raw(virtio_input_event_t* events, uint32_t capacity) {
    uint32_t count = 0;
    if (!ready) return 0;
    while (used.idx != last_used && count < capacity) {
        dmb();
        uint32_t id = used.ring[last_used % VRING_NUM].id;
        last_used++;
        if (id < VRING_NUM) {
            const struct input_event* ev = &events_buf[id];
            if (events) {
                events[count].type = ev->type;
                events[count].code = ev->code;
                events[count].value = ev->value;
            }
            count++;
            avail.ring[avail.idx % VRING_NUM] = (uint16_t)id;
            dmb();
            avail.idx++;
            dmb();
            wr(REG_QUEUE_NOTIFY, 0);
        }
    }
    dmb();
    return (int)count;
}

int virtio_input_poll(char* buffer, uint32_t capacity) {
    uint32_t count = 0;
    if (!ready) return 0;
    while (used.idx != last_used) {
        dmb();
        uint32_t id = used.ring[last_used % VRING_NUM].id;
        last_used++;
        if (id < VRING_NUM) {
            const struct input_event* ev = &events_buf[id];
            if (ev->type == EV_KEY) {
                if (ev->code == KEY_LEFTSHIFT || ev->code == KEY_RIGHTSHIFT) {
                    shifted = ev->value != 0;
                } else if (ev->value == 1u || ev->value == 2u) {
                    char c = translate(ev->code, shifted);
                    if (c && count < capacity)
                        buffer[count++] = c;
                }
            }
            avail.ring[avail.idx % VRING_NUM] = (uint16_t)id;
            dmb();
            avail.idx++;
            dmb();
            wr(REG_QUEUE_NOTIFY, 0);
        }
    }
    dmb();
    return (int)count;
}

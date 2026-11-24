#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../b43_boot.h"

/* Model the indirect SHM window, including its upper-half data register.
 * Values published by the firmware only become visible when the PSM starts. */
typedef struct {
    uint32_t mac, control, reason, ucode[2], pcm[2];
    uint16_t scratch[64], shared[2048], revision, date;
    unsigned reads, writes, cleared, uwords, pwords, polls, respond_after;
    unsigned word_delays, poll_delays, starts, stops, irq_cleared;
} model_t;
static uint8_t ucode[] = {'u',1,0,0,0,0,0,8, 0x12,0x34,0x56,0x78, 0xAB,0xCD,0xEF,1};
static uint8_t pcm[] = {'p',1,0,0,0,0,0,8, 1,2,3,4, 0xFE,0xDC,0xBA,0x98};
static uint32_t read32(void* ctx, uint16_t reg) {
    model_t* m = ctx; m->reads++;
    if (reg == 0x120) return m->mac;
    assert(reg == 0x128);
    m->polls++;
    return m->polls >= m->respond_after ? m->reason : 0;
}
static uint16_t read16(void* ctx, uint16_t reg) {
    model_t* m = ctx; m->reads++;
    assert(m->starts && m->polls >= m->respond_after);
    assert((m->control >> 16) == 1);
    assert(reg == 0x164 || reg == 0x166);
    unsigned index = (m->control & 0xFFFF) * 2 + (reg == 0x166);
    assert(index < 2048);
    return m->shared[index];
}
static void write16(void* ctx, uint16_t reg, uint16_t val) {
    model_t* m = ctx; m->writes++;
    assert(!m->starts && val == 0);
    unsigned route = m->control >> 16, index = m->control & 0xFFFF;
    if (route == 2) {
        assert(reg == 0x164 && index < 64 && m->scratch[index] == 0xFFFF);
        m->scratch[index] = val;
    } else {
        assert(route == 1 && (reg == 0x164 || reg == 0x166));
        index = index * 2 + (reg == 0x166);
        assert(index < 2048 && m->shared[index] == 0xFFFF);
        m->shared[index] = val;
    }
    m->cleared++;
}
static void write32(void* ctx, uint16_t reg, uint32_t val) {
    model_t* m = ctx; m->writes++;
    if (reg == 0x160) { m->control = val; return; }
    if (reg == 0x128) {
        assert(val == 0xFFFFFFFF && m->uwords == 2 && !m->starts);
        m->irq_cleared++; return;
    }
    if (reg == 0x120) {
        assert((val & ~6u) == (m->mac & ~6u));
        if (val & 2) {
            assert(!(val & 4) && m->irq_cleared == 1);
            m->starts++;
            m->shared[0] = m->revision; m->shared[1] = 9;
            m->shared[2] = m->date; m->shared[3] = 17;
        } else {
            assert(val & 4);
            if (m->starts) m->stops++;
        }
        m->mac = val; return;
    }
    assert(reg == 0x164 && m->cleared == 2112 && !m->starts);
    if ((m->control >> 16) == 0x100) {
        assert((m->control & 0xFFFF) == m->uwords && m->uwords < 2);
        m->ucode[m->uwords++] = val; m->control++;
    } else if (m->control == 0x000301EA) {
        assert(val == 0x4000 && !m->pwords);
    } else {
        assert(m->control == 0x000301EB && m->pwords < 2);
        m->pcm[m->pwords++] = val;
    }
}
static void delay(void* ctx, uint32_t us) {
    model_t* m = ctx;
    if (us == 10) m->word_delays++;
    else { assert(us == 50000); m->poll_delays++; }
}
static b43_io_t setup(model_t* m) {
    memset(m, 0, sizeof(*m));
    memset(m->scratch, 0xFF, sizeof(m->scratch));
    memset(m->shared, 0xFF, sizeof(m->shared));
    m->mac = 0x04000500; m->revision = 598; m->date = 0x1234;
    m->reason = 1; m->respond_after = 3;
    return (b43_io_t){m, read16, read32, write16, write32, delay};
}
int main(void) {
    model_t m; b43_boot_info_t info;
    b43_io_t io = setup(&m);
    assert(b43_firmware_boot(&io,ucode,sizeof ucode,pcm,sizeof pcm,&info) == B43_BOOT_OK);
    assert(m.ucode[0] == 0x12345678 && m.ucode[1] == 0xABCDEF01);
    assert(m.pcm[0] == 0x01020304 && m.pcm[1] == 0xFEDCBA98);
    assert(m.word_delays == 4 && m.poll_delays == 2 && m.polls == 4);
    assert(m.starts == 1 && !m.stops && (m.mac & 6) == 2);
    assert(info.revision == 598 && info.patch == 9 && info.header_format == 598 && !info.opensource);
    const unsigned revisions[] = {0x129,409,410,597,598};
    for (unsigned i = 0; i < sizeof revisions / sizeof revisions[0]; i++) {
        io = setup(&m); m.revision = revisions[i]; m.date = 0xFFFF;
        assert(b43_firmware_boot(&io,ucode,sizeof ucode,0,0,&info) == B43_BOOT_OK);
        assert(info.opensource && info.patch == 17 && !m.pwords && m.word_delays == 2);
        assert(info.header_format == (revisions[i] >= 598 ? 598 : revisions[i] >= 410 ? 410 : 351));
    }
    const unsigned bad_mac[] = {0xFFFFFFFF,0x04000502,0x04000501,0x04010500,0x04000100};
    for (unsigned i = 0; i < sizeof bad_mac / sizeof bad_mac[0]; i++) {
        io = setup(&m); m.mac = bad_mac[i];
        int expected = i == 1 || i == 2 ? B43_BOOT_BUSY : B43_BOOT_ACCESS;
        assert(b43_firmware_boot(&io,ucode,sizeof ucode,0,0,&info) == expected);
        assert(!m.writes && !info.revision);
    }
    for (unsigned i = 0; i < sizeof ucode; i++) {
        io = setup(&m);
        assert(b43_firmware_boot(&io,ucode,i,0,0,&info) == B43_BOOT_INVALID);
        assert(!m.reads && !m.writes);
    }
    static uint8_t oversized[8 + 0x40004] = {'u',1,0,0,0,4,0,4};
    io = setup(&m);
    assert(b43_firmware_boot(&io,oversized,sizeof oversized,0,0,&info) == B43_BOOT_INVALID);
    assert(!m.reads && !m.writes);
    io = setup(&m);
    assert(b43_firmware_boot(&io,ucode,sizeof ucode,pcm,sizeof pcm - 1,&info) == B43_BOOT_INVALID);
    assert(!m.reads && !m.writes);
    assert(b43_firmware_boot(&io,ucode,sizeof ucode,0,1,&info) == B43_BOOT_INVALID);
    io.delay_us = 0;
    assert(b43_firmware_boot(&io,ucode,sizeof ucode,0,0,&info) == B43_BOOT_INVALID);
    assert(b43_firmware_boot(0,ucode,sizeof ucode,0,0,&info) == B43_BOOT_INVALID);
    assert(b43_firmware_boot(&io,ucode,sizeof ucode,0,0,0) == B43_BOOT_INVALID);
    for (unsigned mode = 0; mode < 5; mode++) {
        io = setup(&m);
        if (mode == 0) m.respond_after = 100;
        if (mode == 1) m.reason = 3; /* Suspended plus another IRQ is not ready. */
        if (mode == 2) m.reason = 0xFFFFFFFF;
        if (mode == 3) m.revision = 0x128;
        if (mode == 4) m.revision = 0xFFFF;
        int expected = mode < 2 ? B43_BOOT_TIMEOUT : mode == 3 ? B43_BOOT_OLD_FIRMWARE : B43_BOOT_ACCESS;
        assert(b43_firmware_boot(&io,ucode,sizeof ucode,0,0,&info) == expected);
        assert(m.stops == 1 && (m.mac & 6) == 4 && !info.revision);
        if (mode < 2) assert(m.polls == 20 && m.poll_delays == 19);
    }
    puts("b43 firmware boot: SHM, uploads, revisions, timeouts and cleanup passed");
}

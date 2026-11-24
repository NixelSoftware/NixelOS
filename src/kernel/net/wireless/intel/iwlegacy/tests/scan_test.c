#define IL_TRANSPORT_FIXTURE_ONLY
#include "transport_test.c"
#include "../scan.h"
static uint8_t tfds[256 * 64] __attribute__((aligned(256)));
static uint8_t cmd_buffer[4096] __attribute__((aligned(4096)));
typedef struct { transport_t t; unsigned mode, events, commands; uint8_t ids[16]; } scan_model_t;
static unsigned make_beacon(uint8_t* p, unsigned channel, unsigned band, int secure) {
    memset(p,0,256);
    uint8_t* f = p + 16;
    p[2] = 40; p[8] = channel; p[10] = band == 2 ? 1 : 0;
    f[0] = 0x80;
    memset(f + 4,0xFF,6); f[10] = f[16] = 2; f[15] = f[21] = channel;
    f[32] = 100; f[34] = secure ? 17 : 1;
    f[36] = 0; f[37] = 4; memcpy(f + 38,"test",4);
    unsigned length = 42;
    if (secure) {
        const uint8_t rsn[] = {48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
        memcpy(f + length,rsn,sizeof(rsn)); length += sizeof(rsn);
    }
    p[14] = length; p[15] = length >> 8; put32(f + length,3);
    return 32 + length;
}
static void enqueue(scan_model_t* s, unsigned index, uint8_t id, uint8_t flags,
                      uint16_t seq, const uint8_t* data, unsigned length) {
    uint8_t* p = s->t.rx.slots[index].data;
    put32(p,length + 4); p[4] = id; p[5] = flags; p[6] = seq; p[7] = seq >> 8;
    memcpy(p + 8,data,length);
}
static uint32_t scan_read(void* ctx, uint32_t reg) { return transport_read(&((scan_model_t*)ctx)->t,reg); }
static void scan_write(void* ctx, uint32_t reg, uint32_t value) {
    scan_model_t* s = ctx;
    transport_write(&s->t,reg,value);
    if (reg != HBUS_TARG_WRPTR) return;
    if (s->commands < 16) s->ids[s->commands] = cmd_buffer[0];
    s->commands++;
    unsigned index = (value - 1) & 255;
    assert((value & 0xFF00) == 0x400 && il_le32(tfds + index * 64 + 4) == 0x18000);
    unsigned length = il_le32(tfds + index * 64 + 8) - 4;
    assert((il_le32(tfds + index * 64) >> 24 & 15) == 1);
    assert((il_le32(tfds + index * 64) >> 28) == ((0u - length) & 3));
    if (s->mode == 1 || s->mode == 6 || s->mode == 7) return;
    uint16_t seq = il_le16(cmd_buffer + 2);
    uint8_t response[4] = {s->mode == 4 ? 2 : 1,0,0,0};
    unsigned reply_length = cmd_buffer[0] == 0x18 ? 1 : 4;
    if (cmd_buffer[0] == 0x18) {
        if (s->mode == 8) reply_length = 4; /* DMA-padded status */
        if (s->mode == 9) reply_length = 0; /* missing status */
        if (s->mode == 10) response[0] = 3; /* not Linux's success value */
        if (s->mode == 11) response[0] = 2; /* firmware table full */
    }
    unsigned read = s->t.rx.read;
    enqueue(s,read,cmd_buffer[0],s->mode == 3 ? 0x40 : 0,seq ^ (s->mode == 2 ? 1 : 0),response,reply_length);
    status[0] = (read + 1) & 255;
    if (cmd_buffer[0] == 0x80 && s->mode != 4 && s->mode != 5) {
        /* Validate the upstream ABI, rather than echoing the builder's offsets. */
        assert(il_le16(cmd_buffer + 2) & 0x4000); /* even a one-channel scan */
        const uint8_t* scan = cmd_buffer + 4;
        unsigned band = scan[20] & 1 ? 2 : 5;
        unsigned probe = il_le16(scan + 28);
        assert(probe == 26 && scan[216] == 0x40 && !scan[240] && !scan[241]);
        assert(il_le16(scan + 8) == 0xffff);
        assert(il_le32(scan + 32) == (1u << 13) && scan[37] == 24);
        assert(scan[36] == (band == 2 ? 10 : 13));
        assert(il_le32(scan + 68) == 0xffffffffu);
        for (unsigned i = 0; i < scan[3]; i++) {
            const uint8_t* channel = scan + 216 + probe + i * 8;
            assert(!channel[0]); /* no active or directed probe bits */
            assert(channel[2] == (band == 2 ? 0x48 : 0x3b) && channel[3] == 110);
        }
        unsigned channel = scan[216 + probe + 1];
        uint8_t payload[256]; unsigned n = make_beacon(payload,channel,band,1);
        enqueue(s,(read + 1) & 255,0x1B,0,0x8000,payload,n);
        uint8_t complete[12] = {0}; complete[0] = cmd_buffer[7];
        enqueue(s,(read + 2) & 255,0x84,0,0x8000,complete,sizeof(complete));
        status[0] = (read + 3) & 255;
    }
}
static void scan_delay(void* ctx, uint32_t us) {
    scan_model_t* s=ctx; transport_delay(&s->t,us);
    if (s->mode == 7 && !s->events) {
        /* Hold the asynchronous reply until both interrupt banks are acked. */
        if (s->t.m.regs[CSR_INT/4] || s->t.m.regs[CSR_FH_INT_STATUS/4]) return;
    }
    if ((s->mode == 6 || s->mode == 7) && !s->events) {
        /* Real DMA completes later, not synchronously in the doorbell write. */
        assert(s->t.m.regs[CSR_GP_CNTRL/4] & CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
        uint8_t response[4]={1};
        enqueue(s,s->t.rx.read,cmd_buffer[0],0,il_le16(cmd_buffer+2),response,4);
        status[0]=(s->t.rx.read+1)&255; s->events++;
    }
}
static il_io_t scan_setup(scan_model_t* s, il_command_queue_t* q) {
    memset(s,0,sizeof(*s)); memset(tfds,0,sizeof(tfds)); *q = (il_command_queue_t){0};
    (void)transport_setup(&s->t);
    il_io_t io = {s,scan_read,scan_write,scan_delay};
    assert(il3945_rx_start(&io,&s->t.rx) == IL_OK);
    assert(il3945_command_prepare(q,tfds,cmd_buffer,0x18000) == IL_OK);
    return io;
}
int main(void) {
    scan_model_t s; il_command_queue_t q; il_io_t io = scan_setup(&s,&q);
    for (unsigned i = 0; i < 300; i++) {
        uint8_t payload[333] = {0};
        assert(il3945_command_send(&io,&s.t.rx,&q,0x10,payload,i & 1 ? 333 : 44,100,0,0) == IL_OK);
        assert(q.sequence == (uint16_t)(0x400 | (i & 255) | (i & 1 ? 0x4000 : 0)));
    }
    assert(q.next == 44 && s.commands == 300 && !q.pending && !q.poisoned);
    for (unsigned mode = 1; mode <= 3; mode++) {
        io = scan_setup(&s,&q); s.mode = mode;
        assert(il3945_command_send(&io,&s.t.rx,&q,0x10,0,0,1,0,0) == (mode == 3 ? IL_VERIFY : IL_TIMEOUT));
        if (mode < 3) {
            assert(q.poisoned);
            unsigned before = s.commands;
            assert(il3945_command_send(&io,&s.t.rx,&q,0x10,0,0,1,0,0) == IL_INVALID && s.commands == before);
            assert(il3945_command_prepare(&q,tfds,cmd_buffer,0x18000) == IL_INVALID);
        }
    }
    io = scan_setup(&s,&q); s.mode=6;
    assert(il3945_command_send(&io,&s.t.rx,&q,0x77,0,0,100,0,0) == IL_OK);
    assert(s.events == 1 && !q.pending && !q.poisoned);
    assert(!(s.t.m.regs[CSR_GP_CNTRL/4] & CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ));
    io = scan_setup(&s,&q); s.mode=7;
    s.t.m.regs[CSR_INT/4] = CSR_INT_BIT_FH_RX;
    s.t.m.regs[CSR_FH_INT_STATUS/4] = 1u << 16;
    assert(il3945_command_send(&io,&s.t.rx,&q,0x77,0,0,100,0,0) == IL_OK);
    assert(s.events == 1);
    io = scan_setup(&s,&q); s.mode=1;
    s.t.m.regs[CSR_FH_INT_STATUS/4] = CSR_FH_INT_BIT_ERR;
    assert(il3945_command_send(&io,&s.t.rx,&q,0x77,0,0,100,0,0) == IL_VERIFY);
    assert(q.pending && q.poisoned && q.timeout_fh == (uint32_t)CSR_FH_INT_BIT_ERR);
    io = scan_setup(&s,&q); s.mode=1;
    s.t.m.regs[CSR_UCODE_DRV_GP1/4] = CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED;
    assert(il3945_command_send(&io,&s.t.rx,&q,0x77,0,0,1,0,0) == IL_TIMEOUT);
    assert(q.pending && q.poisoned && q.timeout_gp1 == CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);
    assert(q.timeout_gp & CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
    assert(q.timeout_rx_read == s.t.rx.read && q.timeout_rx_closed == status[0]);
    assert(!(s.t.m.regs[CSR_GP_CNTRL/4] & CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ));
    uint8_t ee[IL3945_EEPROM_SIZE]; eeprom_image(ee); il3945_eeprom_t eeprom;
    assert(il3945_eeprom_parse(ee,sizeof(ee),0,&eeprom));
    uint8_t out[1024]; unsigned length;
    assert(il3945_scan_build(&eeprom,2,out,sizeof(out),&length) == IL_OK);
    assert(length == 250 && out[3] == 1 && out[8] == 0xFF && out[9] == 0xFF);
    assert(!out[242] && out[243] == 1 && out[244] == 0x48 && out[245] == 110);
    for (unsigned i = 80; i < 216; i++) assert(out[i] == 0); /* no directed SSIDs */
    assert(!memcmp(out + 226,eeprom.mac,6));
    assert(il3945_scan_build(&eeprom,2,out,249,&length) == IL_INVALID && !length);
    for (unsigned mode = 0; mode <= 5; mode += mode < 4 ? 4 : 1) {
        io = scan_setup(&s,&q); s.mode = mode;
        il_scan_results_t results;
        int result = il3945_scan_run(&io,&s.t.rx,&q,&eeprom,&results);
        assert(result == (mode == 0 ? IL_OK : mode == 4 ? IL_VERIFY : IL_TIMEOUT));
        if (!mode) {
            assert(results.count == 2 && results.complete && s.commands == 2);
            assert(results.networks[0].security == IL_SECURITY_WPA2_PSK && results.networks[0].signal_dbm == -55);
            assert(results.networks[0].channel == 1 && results.networks[1].channel == 183);
        }
    }
    /* Verify the missing post-RXON commands precede scanning, and calibration
     * failure never posts an uncalibrated power table or scan. */
    for (unsigned g = 0; g < 5; g++) {
        uint8_t* group = eeprom.calibration + 64*g;
        for (unsigned i = 0; i < 5; i++) { group[4*i] = 10+i*10; group[4*i+1] = 40-i*10; }
        group[60] = 40; group[61] = 40+g*20;
        group[62] = (uint8_t)-100; group[63] = 255;
    }
    io = scan_setup(&s,&q);
    assert(!il3945_scan_configure(&io,&s.t.rx,&q,&eeprom,2,1));
    assert(s.commands == 4 && s.ids[0] == 0x97 && s.ids[1] == 0x18 &&
           s.ids[2] == 0x47 && s.ids[3] == 0x47);
    il_scan_results_t configured;
    assert(!il3945_scan_run(&io,&s.t.rx,&q,&eeprom,&configured));
    assert(s.ids[4] == 0x80 && s.ids[5] == 0x80);
    io = scan_setup(&s,&q); s.mode = 1;
    assert(il3945_scan_configure(&io,&s.t.rx,&q,&eeprom,2,1) == IL_TIMEOUT);
    assert(s.commands == 1 && q.command == 0x97 && q.poisoned);
    for (unsigned mode = 8; mode <= 11; mode++) {
        io = scan_setup(&s,&q); s.mode = mode;
        assert(il3945_scan_configure(&io,&s.t.rx,&q,&eeprom,2,1) ==
               (mode == 8 ? IL_OK : IL_VERIFY));
        if (mode != 8) assert(q.command == 0x18 && !q.pending && !q.poisoned);
    }
    io = scan_setup(&s,&q); eeprom.calibration[60] = 0;
    assert(il3945_scan_configure(&io,&s.t.rx,&q,&eeprom,2,1) == IL_BAD_EEPROM);
    assert(!s.commands);
    /* Independently validate packed and DMA-padded completion notifications. */
    for (unsigned n = 0; n <= 12; n++) {
        il_scan_results_t completion = {0}; completion.expected_channels = 13;
        uint8_t bytes[12] = {13};
        il3945_scan_event(&completion,0x84,0,0x8000,bytes,n);
        assert(completion.completion_length == n);
        assert(completion.complete == (n >= 11));
        assert(completion.error == (n < 11 ? IL_VERIFY : IL_OK));
    }
    il_scan_results_t mismatch = {0}; mismatch.expected_channels = 13;
    uint8_t short_scan[12] = {12};
    il3945_scan_event(&mismatch,0x84,0,0x8000,short_scan,sizeof(short_scan));
    assert(mismatch.error == IL_VERIFY && !mismatch.complete && mismatch.completed_channels == 12);
    il_scan_results_t results = {0}; results.active_band = 2; results.allowed_channels[6] = 1;
    uint8_t payload[256]; unsigned size = make_beacon(payload,6,2,0);
    il3945_scan_event(&results,0x1B,0,0x8000,payload,size);
    assert(results.count == 1 && results.networks[0].security == IL_SECURITY_OPEN);
    for (unsigned n = 0; n < size - 16; n++) {
        unsigned before = results.count;
        il3945_scan_event(&results,0x1B,0,0x8000,payload,n);
        assert(results.count == before);
    }
    put32(payload + size - 16,0); results.count = 0;
    il3945_scan_event(&results,0x1B,0,0x8000,payload,size); assert(!results.count);
    size = make_beacon(payload,6,2,1); payload[16 + 43] = 255;
    il3945_scan_event(&results,0x1B,0,0x8000,payload,size); assert(!results.count);
    size = make_beacon(payload,7,2,0);
    il3945_scan_event(&results,0x1B,0,0x8000,payload,size); assert(!results.count);
    puts("iwlegacy commands/scans: completion matching, wrap, poison, passive permissions and beacon bounds passed");
    return 0;
}

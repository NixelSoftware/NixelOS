#define IL_TRANSPORT_FIXTURE_ONLY
#include "transport_test.c"
#include "../scan.h"
static uint8_t tfds[256 * 64] __attribute__((aligned(256)));
static uint8_t cmd_buffer[4096] __attribute__((aligned(4096)));
typedef struct { transport_t t; unsigned mode, events, commands; } scan_model_t;
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
    s->commands++;
    unsigned index = (value - 1) & 255;
    assert((value & 0xFF00) == 0x400 && il_le32(tfds + index * 64 + 4) == 0x18000);
    unsigned length = il_le32(tfds + index * 64 + 8) - 4;
    assert((il_le32(tfds + index * 64) >> 24 & 15) == 1);
    assert((il_le32(tfds + index * 64) >> 28) == ((0u - length) & 3));
    if (s->mode == 1) return;
    uint16_t seq = il_le16(cmd_buffer + 2);
    uint8_t response[4] = {s->mode == 4 ? 2 : 1,0,0,0};
    unsigned read = s->t.rx.read;
    enqueue(s,read,cmd_buffer[0],s->mode == 3 ? 0x40 : 0,seq ^ (s->mode == 2 ? 1 : 0),response,4);
    status[0] = (read + 1) & 255;
    if (cmd_buffer[0] == 0x80 && s->mode != 4 && s->mode != 5) {
        unsigned band = cmd_buffer[4 + 20] & 1 ? 2 : 5;
        unsigned channel = cmd_buffer[4 + 216 + 1];
        uint8_t payload[256]; unsigned n = make_beacon(payload,channel,band,1);
        enqueue(s,(read + 1) & 255,0x1B,0,0x8000,payload,n);
        uint8_t complete[11] = {0}; complete[0] = cmd_buffer[7];
        enqueue(s,(read + 2) & 255,0x84,0,0x8000,complete,sizeof(complete));
        status[0] = (read + 3) & 255;
    }
}
static void scan_delay(void* ctx, uint32_t us) { transport_delay(&((scan_model_t*)ctx)->t,us); }
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
    uint8_t ee[IL3945_EEPROM_SIZE]; eeprom_image(ee); il3945_eeprom_t eeprom;
    assert(il3945_eeprom_parse(ee,sizeof(ee),0,&eeprom));
    uint8_t out[1024]; unsigned length;
    assert(il3945_scan_build(&eeprom,2,out,sizeof(out),&length) == IL_OK);
    assert(length == 224 && out[3] == 1 && out[8] == 0xFF && out[9] == 0xFF);
    assert(!out[216] && out[217] == 1 && !out[218] && !out[219]);
    for (unsigned i = 28; i < 216; i++) assert(out[i] == 0); /* no TX/probes */
    assert(il3945_scan_build(&eeprom,2,out,223,&length) == IL_INVALID && !length);
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

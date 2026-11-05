/* Reuse the EEPROM/peripheral fixture; this test drives actual RX/session code. */
#define main earlier_3945_tests
#include "3945_test.c"
#undef main
#include "../session.h"
#include "../fh.h"
typedef struct {
    model_t m;
    il_rx_queue_t rx;
    unsigned phase, memory_offset, mode, dispatched;
    uint8_t firmware[44];
} transport_t;
static uint8_t pages[254][IL_RX_BYTES];
static uint32_t descriptors[256] __attribute__((aligned(256)));
static uint16_t status[6] __attribute__((aligned(16)));
static il_rx_buffer_t buffers[254];
static void alive_event(transport_t* t, unsigned subtype, unsigned valid) {
    uint8_t* p = t->rx.slots[t->rx.read].data;
    memset(p,0,40); put32(p,36); p[4] = 1; p[8] = 3; p[9] = 15;
    p[8 + 13] = subtype; put32(p + 8 + 28,valid);
    put32(p + 8 + 16,0x800100); put32(p + 8 + 20,0x800200);
    status[0] = (t->rx.read + 1) & 255;
}
static uint32_t transport_read(void* ctx, uint32_t reg) {
    transport_t* t = ctx;
    if (reg == FH39_RSSR_STATUS) return t->mode == 10 ? 0 : FH39_RSSR_CHNL0_RX_STATUS_CHNL_IDLE;
    if (reg == HBUS_TARG_MEM_RDAT) {
        assert(t->memory_offset == 0); t->memory_offset += 4;
        uint32_t word = il_le32(t->firmware + (t->phase == 1 ? 32 : 24));
        return t->mode == 5 ? word ^ 1 : word;
    }
    if (reg == CSR_INT && t->phase && t->mode == 6) return CSR_INT_BIT_SW_ERR;
    return read32(&t->m,reg);
}
static void transport_write(void* ctx, uint32_t reg, uint32_t value) {
    transport_t* t = ctx;
    if (reg == CSR_INT) { t->m.regs[reg / 4] = 0; return; } /* W1C */
    write32(&t->m,reg,value);
    if (reg == HBUS_TARG_MEM_RADDR) { assert(value == 0); t->memory_offset = 0; }
    if (reg == CSR_RESET && !value) {
        t->phase = 1;
        if (t->mode == 1) return;
        alive_event(t,t->mode == 3 ? 0 : 9,t->mode == 4 ? 0 : 1);
        if (t->mode == 7) put32(t->rx.slots[t->rx.read].data,35);
    }
    if (reg == HBUS_TARG_PRPH_WDAT && t->m.pw == BSM_DRAM_INST_BYTECOUNT_REG &&
        (value & BSM_DRAM_INST_LOAD)) {
        assert(t->m.prph[BSM_DRAM_INST_PTR_REG / 4] == 0x120000);
        assert(t->m.prph[BSM_DRAM_DATA_PTR_REG / 4] == 0x130000);
        assert(t->m.prph[BSM_DRAM_DATA_BYTECOUNT_REG / 4] == 4);
        t->phase = 2;
        if (t->mode != 2) alive_event(t,0,1);
    }
}
static void transport_delay(void* ctx, uint32_t us) {
    transport_t* t = ctx; delay_us(&t->m,us);
    if (t->mode == 8 && t->phase) t->m.regs[CSR_GP_CNTRL / 4] &= ~CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW;
}
static il_io_t transport_setup(transport_t* t) {
    memset(t,0,sizeof(*t)); (void)setup(&t->m);
    firmware_image(t->firmware);
    t->m.regs[CSR_GP_CNTRL / 4] |= CSR_GP_CNTRL_REG_FLAG_INIT_DONE;
    for (unsigned i = 0; i < 254; i++) buffers[i] = (il_rx_buffer_t){pages[i],0x200000 + i * IL_RX_BYTES};
    assert(il3945_rx_prepare(&t->rx,descriptors,0x10000,status,0x11000,buffers) == IL_OK);
    return (il_io_t){t,transport_read,transport_write,transport_delay};
}
static void __attribute__((unused)) packet(void* ctx, uint8_t command, uint8_t flags, uint16_t sequence,
                     const uint8_t* payload, unsigned length) {
    transport_t* t = ctx;
    assert(command == 0x82 && flags == 0 && sequence == 0x1234);
    assert(length == 4 && il_le32(payload) == 0xABCDEF01); t->dispatched++;
}
#ifndef IL_TRANSPORT_FIXTURE_ONLY
int main(void) {
    transport_t t; il_io_t io = transport_setup(&t);
    assert(t.rx.write == 254 && descriptors[253] == buffers[253].dma && !descriptors[254]);
    assert(il3945_rx_start(&io,&t.rx) == IL_OK && t.rx.published == 248);
    assert(t.m.regs[FH39_RCSR_RBD_BASE(0) / 4] == 0x10000);
    assert(t.m.regs[FH39_RCSR_RPTR_ADDR(0) / 4] == 0x11000);
    assert(t.m.regs[FH39_RCSR_CONFIG(0) / 4] == 0xA9801010);
    assert(il3945_rx_start(&io,&t.rx) == IL_INVALID);
    for (unsigned round = 0; round < 64; round++) {
        for (unsigned n = 0; n < 16; n++) {
            uint8_t* p = t.rx.slots[(t.rx.read + n) & 255].data;
            put32(p,8); p[4] = 0x82; p[5] = 0; p[6] = 0x34; p[7] = 0x12;
            put32(p + 8,0xABCDEF01);
        }
        status[0] = (t.rx.read + 16) & 255;
        assert(il3945_rx_poll(&io,&t.rx,packet,&t) == 16);
    }
    assert(t.dispatched == 1024 && t.rx.read == 0 && t.rx.write == 254 && t.rx.published == 248);
    put32(t.rx.slots[0].data,0x3FFF); status[0] = 1;
    assert(il3945_rx_poll(&io,&t.rx,packet,&t) == 1 && t.rx.malformed == 1 && t.dispatched == 1024);
    status[0] = 0xFFF;
    assert(il3945_rx_poll(&io,&t.rx,packet,&t) == IL_VERIFY);
    status[0] = t.rx.read;
    t.mode = 10;
    assert(il3945_rx_stop(&io,&t.rx) == IL_TIMEOUT && t.rx.active);
    t.mode = 0; assert(il3945_rx_stop(&io,&t.rx) == IL_OK && !t.rx.active);
    buffers[1].dma = buffers[0].dma;
    assert(il3945_rx_prepare(&t.rx,descriptors,0x10000,status,0x11000,buffers) == IL_INVALID);
    io = transport_setup(&t);
    buffers[1].dma = 0x10000;
    assert(il3945_rx_prepare(&t.rx,descriptors,0x10000,status,0x11000,buffers) == IL_INVALID);
    io = transport_setup(&t); t.m.clock_stall = 1;
    assert(il3945_rx_start(&io,&t.rx) == IL_TIMEOUT && !t.rx.active);
    for (unsigned mode = 0; mode <= 8; mode++) {
        io = transport_setup(&t); t.mode = mode;
        assert(il3945_rx_start(&io,&t.rx) == IL_OK);
        uint32_t shared[8]; uint32_t rings[5] = {0x400000,0x404000,0x408000,0x40C000,0x410000};
        assert(il3945_tx_setup(&io,shared,0x15000,rings) == IL_OK);
        assert(shared[0] == rings[0] && shared[4] == rings[4] && shared[7] == 0);
        assert(t.m.regs[FH39_TCSR_CONFIG(4) / 4] == 0x80200008);
        il3945_fw_dma_t dma = {0x100000,0x110000,0x120000,0x130000};
        il3945_alive_t alive;
        int result = il3945_firmware_run(&io,&t.rx,t.firmware,sizeof(t.firmware),&dma,&alive);
        int expected = mode == 0 ? IL_OK : mode < 3 ? IL_TIMEOUT : mode == 8 ? IL_RFKILL : IL_VERIFY;
        assert(result == expected);
        if (!mode) {
            assert(alive.init_alive && alive.runtime_alive && alive.major == 15 && alive.minor == 3);
            assert(alive.event_log == 0x800100 && alive.error_log == 0x800200);
        }
        assert(il3945_rx_stop(&io,&t.rx) == IL_OK);
    }
    io = transport_setup(&t); assert(il3945_rx_start(&io,&t.rx) == IL_OK);
    il3945_fw_dma_t dma = {0x100000,0x110000,0x120000,0x130000}; il3945_alive_t alive;
    dma.runtime_code = 0;
    unsigned before = t.m.writes;
    assert(il3945_firmware_run(&io,&t.rx,t.firmware,sizeof(t.firmware),&dma,&alive) == IL_INVALID);
    assert(t.m.writes == before);
    puts("iwlegacy transport: RX wrap/recycling, malformed DMA, TX setup, ALIVE transition and failures passed");
    return 0;
}

#endif

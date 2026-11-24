#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../internal.h"

static void put32(uint8_t* p, uint32_t n) {
    p[0] = n; p[1] = n >> 8; p[2] = n >> 16; p[3] = n >> 24;
}
static void eeprom_image(uint8_t* p) {
    memset(p, 0, IL3945_EEPROM_SIZE);
    const uint8_t mac[] = {0x00,0x13,0x02,0xAB,0xCD,0xEF};
    memcpy(p + 42, mac, 6); p[136] = 0x2F; p[106] = 0xD1;
    p[198] = 9; p[199] = 17; /* channel 1: valid, active scan */
    p[228] = 1; p[229] = (uint8_t)-3; /* 4.9 GHz channel 183 */
    p[306] = 0x81; p[307] = 10; /* channel 145: valid, DFS */
}
static void firmware_image(uint8_t* p) {
    memset(p, 0, 44); put32(p, 0x0F000208);
    for (unsigned i = 0; i < 5; i++) {
        put32(p + 4 + i * 4, 4); put32(p + 24 + i * 4, 0x12345600 + i);
    }
}
typedef struct {
    uint32_t regs[1024], prph[0x4000 / 4];
    uint8_t eeprom[IL3945_EEPROM_SIZE];
    unsigned pr, pw, offset, reads, writes, eeprom_reads, delay, bsm_reads;
    int clock_stall, access_stall, eeprom_stall, stop_stall, bsm_stall, corrupt;
    uint32_t unavailable;
} model_t;
static uint32_t read32(void* ctx, uint32_t reg) {
    model_t* m = ctx; m->reads++;
    assert(reg < sizeof(m->regs) && !(reg & 3));
    if (reg == m->unavailable) return 0xFFFFFFFF;
    if (reg == CSR_GP_CNTRL) {
        uint32_t v = m->regs[reg / 4];
        if (!m->clock_stall && (v & CSR_GP_CNTRL_REG_FLAG_INIT_DONE))
            v |= CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY;
        if (m->access_stall) v |= CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP;
        return v;
    }
    if (reg == CSR_RESET && !m->stop_stall && (m->regs[reg / 4] & CSR_RESET_REG_FLAG_STOP_MASTER))
        return m->regs[reg / 4] | CSR_RESET_REG_FLAG_MASTER_DISABLED;
    if (reg == CSR_EEPROM_REG) {
        assert(!(m->regs[CSR_EEPROM_GP / 4] & CSR_EEPROM_GP_IF_OWNER_MSK));
        m->eeprom_reads++;
        if (m->eeprom_stall) return 0;
        return (uint32_t)il_le16(m->eeprom + m->offset) << 16 | 1;
    }
    if (reg == HBUS_TARG_PRPH_RDAT) {
        assert(m->regs[CSR_GP_CNTRL / 4] & CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
        assert(m->pr < sizeof(m->prph));
        uint32_t v = m->prph[m->pr / 4];
        if (m->pr == BSM_WR_CTRL_REG && (v & BSM_WR_CTRL_REG_BIT_START)) {
            if (++m->bsm_reads == 3 && !m->bsm_stall) m->prph[m->pr / 4] = v = 0;
        }
        if (m->pr >= BSM_SRAM_LOWER_BOUND && m->corrupt) v ^= 1;
        return v;
    }
    return m->regs[reg / 4];
}
static void write32(void* ctx, uint32_t reg, uint32_t value) {
    model_t* m = ctx; m->writes++;
    assert(reg < sizeof(m->regs) && !(reg & 3));
    if (reg == HBUS_TARG_PRPH_RADDR || reg == HBUS_TARG_PRPH_WADDR) {
        assert((value >> 24) == 3 && !(value & 3));
        if (reg == HBUS_TARG_PRPH_RADDR) m->pr = value & 0xFFFF;
        else m->pw = value & 0xFFFF;
    }
    if (reg == HBUS_TARG_PRPH_WDAT) {
        assert(m->regs[CSR_GP_CNTRL / 4] & CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
        assert(m->pw < sizeof(m->prph)); m->prph[m->pw / 4] = value;
    }
    if (reg == CSR_EEPROM_REG) {
        assert(value < 2048 && !(value & 3)); m->offset = value >> 1;
    }
    m->regs[reg / 4] = value;
}
static void delay_us(void* ctx, uint32_t delay) { ((model_t*)ctx)->delay += delay; }
static il_io_t setup(model_t* m) {
    memset(m, 0, sizeof(*m)); eeprom_image(m->eeprom);
    m->regs[CSR_HW_REV / 4] = 0x45;
    m->regs[CSR_EEPROM_GP / 4] = 2 | CSR_EEPROM_GP_IF_OWNER_MSK;
    m->regs[CSR_GP_CNTRL / 4] = CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW;
    m->unavailable = 0xFFFFFFFF;
    return (il_io_t){m,read32,write32,delay_us};
}
int main(void) {
    assert(il3945_matches(0x8086,0x4222) && il3945_matches(0x8086,0x4227));
    assert(!il3945_matches(0x14E4,0x4222) && !il3945_matches(0x8086,0x4229));
    assert(il3945_bg_only(0x4222,0x1005) && il3945_bg_only(0x4227,0x1014));
    assert(!il3945_bg_only(0x4222,0x1014));
    uint8_t image[IL3945_EEPROM_SIZE]; eeprom_image(image);
    il3945_eeprom_t e;
    assert(il3945_eeprom_parse(image,sizeof image,0,&e));
    assert(e.version == 0x2F && e.board_revision == 0xD1 && e.mac[5] == 0xEF);
    assert(e.valid_channels == 3 && e.channels[14].number == 183 && e.channels[14].max_power_dbm == -3);
    assert(e.channels[50].number == 145 && e.channels[50].flags == 0x81);
    assert(e.channels[55].number == 165 && e.channels[13].band == 2);
    assert(il3945_eeprom_parse(image,sizeof image,1,&e) && e.valid_channels == 1 && !e.channels[14].flags);
    image[42] = 1; assert(!il3945_eeprom_parse(image,sizeof image,0,&e) && !e.valid_channels);
    image[42] = 0; image[136] = 0x2E; assert(!il3945_eeprom_parse(image,sizeof image,0,&e));
    assert(!il3945_eeprom_parse(image,sizeof image - 1,0,&e));
    assert(!il3945_eeprom_parse(0,sizeof image,0,&e));
    assert(!il3945_eeprom_parse(image,sizeof image,0,0));
    eeprom_image(image); memset(image + 42,0,6);
    assert(!il3945_eeprom_parse(image,sizeof image,0,&e));
    memset(image + 42,0xFF,6); assert(!il3945_eeprom_parse(image,sizeof image,0,&e));
    uint8_t firmware[44]; firmware_image(firmware);
    il3945_firmware_t fw;
    assert(il3945_firmware_parse(firmware,sizeof firmware,&fw));
    assert(fw.version == 0x0F000208 && fw.init_code.data == firmware + 32 && fw.bootstrap.length == 4);
    for (unsigned n = 0; n < sizeof firmware; n++) {
        assert(!il3945_firmware_parse(firmware,n,&fw)); assert(!fw.version && !fw.runtime_code.data);
    }
    for (unsigned field = 0; field < 5; field++) {
        const uint32_t bad[] = {0,3,0xFFFFFFFF,0x14004};
        for (unsigned i = 0; i < 4; i++) {
            firmware_image(firmware); put32(firmware + 4 + field * 4,bad[i]);
            assert(!il3945_firmware_parse(firmware,sizeof firmware,&fw));
        }
    }
    firmware_image(firmware); firmware[1] = 3;
    assert(!il3945_firmware_parse(firmware,sizeof firmware,&fw));
    firmware[1] = 1; assert(il3945_firmware_parse(firmware,sizeof firmware,&fw));
    assert(!il3945_firmware_parse(0,sizeof firmware,&fw));
    assert(!il3945_firmware_parse(firmware,sizeof firmware,0));
    model_t model; il_io_t io = setup(&model); iwlegacy_info_t info = {0};
    assert(il3945_inspect(&io,0,&info) == IL_OK);
    assert(info.state == IL_EEPROM_READY && !info.rfkill && info.hardware_revision == 0x45);
    assert(info.eeprom.valid_channels == 3 && model.eeprom_reads == 1024);
    assert(!(model.regs[CSR_GP_CNTRL / 4] & (CSR_GP_CNTRL_REG_FLAG_INIT_DONE | CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ)));
    assert(model.regs[CSR_RESET / 4] & CSR_RESET_REG_FLAG_SW_RESET);
    assert(model.prph[APMG_CLK_EN_REG / 4] == (APMG_CLK_VAL_DMA_CLK_RQT | APMG_CLK_VAL_BSM_CLK_RQT));
    assert(!(model.prph[APMG_PS_CTRL_REG / 4] & APMG_PS_CTRL_VAL_RESET_REQ));
    io = setup(&model); model.regs[CSR_GP_CNTRL / 4] = 0;
    assert(il3945_inspect(&io,1,&info) == IL_OK && info.rfkill && info.eeprom.valid_channels == 1);
    io = setup(&model); model.stop_stall = 1;
    assert(il3945_apm_stop(&io) == IL_TIMEOUT); /* Strict live-DMA stop. */
    assert(il3945_reset_unowned(&io) == IL_OK); /* No posted host DMA. */
    io = setup(&model); model.unavailable = CSR_RESET;
    assert(il3945_reset_unowned(&io) == IL_ACCESS); /* Never hide bad MMIO. */
    for (unsigned mode = 0; mode < 7; mode++) {
        io = setup(&model);
        if (mode == 0) model.clock_stall = 1;
        if (mode == 1) model.access_stall = 1;
        if (mode == 2) model.regs[CSR_EEPROM_GP / 4] = 0;
        if (mode == 3) model.eeprom_stall = 1;
        if (mode == 4) model.eeprom[136] = 0x2E;
        if (mode == 5) model.stop_stall = 1;
        if (mode == 6) model.unavailable = CSR_EEPROM_REG;
        int expected = mode == 5 ? IL_OK : mode == 2 || mode == 4 ? IL_BAD_EEPROM : mode == 6 ? IL_ACCESS : IL_TIMEOUT;
        assert(il3945_inspect(&io,0,&info) == expected);
        if (expected) assert(info.state != IL_EEPROM_READY && !info.eeprom.version);
        else assert(info.state == IL_EEPROM_READY && info.eeprom.version);
        assert(!(model.regs[CSR_GP_CNTRL / 4] & (CSR_GP_CNTRL_REG_FLAG_INIT_DONE | CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ)));
        assert(model.delay <= 41000 && (model.regs[CSR_RESET / 4] & CSR_RESET_REG_FLAG_SW_RESET));
    }
    io = setup(&model); model.unavailable = CSR_HW_REV;
    assert(il3945_inspect(&io,0,&info) == IL_ACCESS && !model.writes);
    assert(il3945_inspect(0,0,&info) == IL_INVALID);
    firmware_image(firmware);
    for (unsigned mode = 0; mode < 4; mode++) {
        io = setup(&model); model.regs[CSR_GP_CNTRL / 4] |= CSR_GP_CNTRL_REG_FLAG_INIT_DONE;
        if (mode == 1) model.corrupt = 1;
        if (mode == 2) model.bsm_stall = 1;
        if (mode == 3) model.access_stall = 1;
        int expected = mode == 1 ? IL_VERIFY : mode >= 2 ? IL_TIMEOUT : IL_OK;
        assert(il3945_load_bsm(&io,firmware,sizeof firmware,0x100000,0x110000) == expected);
        assert(!(model.regs[CSR_GP_CNTRL / 4] & CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ));
        if (!mode) {
            assert(model.prph[BSM_DRAM_INST_PTR_REG / 4] == 0x100000);
            assert(model.prph[BSM_DRAM_DATA_BYTECOUNT_REG / 4] == 4);
            assert(model.prph[BSM_SRAM_LOWER_BOUND / 4] == 0x12345604);
            assert(model.prph[BSM_WR_DWCOUNT_REG / 4] == 1);
            assert(model.prph[BSM_WR_CTRL_REG / 4] == BSM_WR_CTRL_REG_BIT_START_EN);
        } else assert(!(model.prph[BSM_WR_CTRL_REG / 4] & BSM_WR_CTRL_REG_BIT_START_EN));
    }
    io = setup(&model);
    assert(il3945_load_bsm(&io,firmware,sizeof firmware,0,0x1000) == IL_INVALID);
    assert(il3945_load_bsm(&io,firmware,sizeof firmware,1,0x1000) == IL_INVALID);
    assert(il3945_load_bsm(&io,firmware,sizeof firmware - 1,0x1000,0x2000) == IL_INVALID);
    assert(!model.reads && !model.writes);
    puts("iwlegacy 3945: IDs, firmware, EEPROM/regulatory data, APM, BSM and failure cleanup passed");
    return 0;
}

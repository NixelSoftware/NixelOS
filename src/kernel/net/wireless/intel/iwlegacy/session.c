/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 * Native polled firmware ALIVE transition from Linux iwlegacy/3945-mac.c,
 * and scheduler/flow-handler setup from 3945.c.
 */
#include "internal.h"
#include "session.h"
#include "fh.h"
static int dma_valid(uint32_t address, unsigned length) {
    return address && !(address & 3) && length && address <= 0xFFFFFFFFu - (length - 1);
}
int il3945_tx_setup(const il_io_t* io, volatile uint32_t* shared, uint32_t shared_dma,
                     const uint32_t rings[5]) {
    if (!il_io_valid(io) || !shared || ((unsigned long)shared & 3) ||
        !dma_valid(shared_dma,32) || (shared_dma & 31) || !rings) return IL_INVALID;
    for (unsigned i = 0; i < 5; i++) {
        if (!dma_valid(rings[i],16384) || (rings[i] & 255)) return IL_INVALID;
        uint32_t a = rings[i], b = shared_dma;
        if (a <= b ? b-a < 16384 : a-b < 32) return IL_INVALID;
        for (unsigned j = 0; j < i; j++) {
            b = rings[j];
            if (a <= b ? b-a < 16384 : a-b < 16384) return IL_INVALID;
        }
    }
    int result = il_grab(io);
    if (result) return result;
    for (unsigned i = 0; i < 8; i++) shared[i] = i < 5 ? rings[i] : 0;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    il_prph_write(io, ALM_SCD_MODE_REG, 2);
    il_prph_write(io, ALM_SCD_ARASTAT_REG, 1);
    il_prph_write(io, ALM_SCD_TXFACT_REG, 0x3F);
    il_prph_write(io, ALM_SCD_SBYP_MODE_1_REG, 0x010000);
    il_prph_write(io, ALM_SCD_SBYP_MODE_2_REG, 0x030002);
    il_prph_write(io, ALM_SCD_TXF4MF_REG, 4);
    il_prph_write(io, ALM_SCD_TXF5MF_REG, 5);
    io->write32(io->context, FH39_TSSR_CBB_BASE, shared_dma);
    io->write32(io->context, FH39_TSSR_MSG_CONFIG,
        FH39_TSSR_TX_MSG_CONFIG_REG_VAL_SNOOP_RD_TXPD_ON |
        FH39_TSSR_TX_MSG_CONFIG_REG_VAL_ORDER_RD_TXPD_ON |
        FH39_TSSR_TX_MSG_CONFIG_REG_VAL_MAX_FRAG_SIZE_128B |
        FH39_TSSR_TX_MSG_CONFIG_REG_VAL_SNOOP_RD_TFD_ON |
        FH39_TSSR_TX_MSG_CONFIG_REG_VAL_ORDER_RD_CBB_ON |
        FH39_TSSR_TX_MSG_CONFIG_REG_VAL_ORDER_RSP_WAIT_TH |
        FH39_TSSR_TX_MSG_CONFIG_REG_VAL_RSP_WAIT_TH);
    for (unsigned i = 0; i < 5; i++) {
        io->write32(io->context, FH39_CBCC_CTRL(i),0);
        io->write32(io->context, FH39_CBCC_BASE(i),0);
        io->write32(io->context, FH39_TCSR_CONFIG(i),
            FH39_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_IFTFD |
            FH39_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE_VAL |
            FH39_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE);
    }
    (void)io->read32(io->context,FH39_TSSR_CBB_BASE);
    il_release(io);
    return IL_OK;
}
typedef struct {
    il3945_alive_t* out;
    unsigned stage, received;
    int error;
} alive_wait_t;
static void notification(void* ctx, uint8_t command, uint8_t flags, uint16_t sequence,
                          const uint8_t* payload, unsigned length) {
    alive_wait_t* wait = ctx;
    (void)flags; (void)sequence;
    if (wait->error) return;
    if (command == 2) { wait->error = IL_VERIFY; return; } /* N_ERROR */
    if (command != 1) return; /* N_ALIVE */
    if (length != 32 || il_le32(payload + 28) != 1 ||
        (payload[13] == 9) != (wait->stage == 0) || wait->received) {
        wait->error = IL_VERIFY; return;
    }
    wait->received = 1;
    if (!wait->stage) wait->out->init_alive = 1;
    else {
        wait->out->runtime_alive = 1;
        wait->out->minor = payload[0]; wait->out->major = payload[1];
        wait->out->event_log = il_le32(payload + 16);
        wait->out->error_log = il_le32(payload + 20);
    }
}
static int radio_enabled(const il_io_t* io) {
    uint32_t gp = io->read32(io->context,CSR_GP_CNTRL);
    if (gp == 0xFFFFFFFFu) return IL_ACCESS;
    return gp & CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW ? IL_OK : IL_RFKILL;
}
static int await_alive(const il_io_t* io, il_rx_queue_t* rx, alive_wait_t* wait) {
    for (unsigned poll = 0; poll < 20000; poll++) {
        int result = radio_enabled(io);
        if (result) return result;
        uint32_t irq = io->read32(io->context,CSR_INT);
        if (irq == 0xFFFFFFFFu) return IL_ACCESS;
        if (irq & (CSR_INT_BIT_HW_ERR | CSR_INT_BIT_SW_ERR)) return IL_VERIFY;
        result = il_ack_interrupts(io,irq);
        if (result) return result;
        result = il3945_rx_poll(io,rx,notification,wait);
        if (result < 0) return result;
        if (wait->error) return wait->error;
        if (wait->received) return IL_OK;
        io->delay_us(io->context,100);
    }
    return IL_TIMEOUT;
}
static int verify_image(const il_io_t* io, const il_fw_section_t* image) {
    int result = il_grab(io);
    if (result) return result;
    io->write32(io->context,HBUS_TARG_MEM_RADDR,0);
    for (unsigned offset = 0; offset < image->length; offset += 4) {
        uint32_t word = io->read32(io->context,HBUS_TARG_MEM_RDAT);
        if (word != il_le32(image->data + offset)) { result = IL_VERIFY; break; }
    }
    il_release(io);
    return result;
}
int il3945_firmware_run(const il_io_t* io, il_rx_queue_t* rx,
                         const uint8_t* firmware, uint32_t size,
                         const il3945_fw_dma_t* dma, il3945_alive_t* alive) {
    il3945_firmware_t fw;
    if (!alive) return IL_INVALID;
    *alive = (il3945_alive_t){0};
    if (!il_io_valid(io) || !rx || !rx->active || !dma ||
        !il3945_firmware_parse(firmware,size,&fw) ||
        !dma_valid(dma->init_code,fw.init_code.length) ||
        !dma_valid(dma->init_data,fw.init_data.length) ||
        !dma_valid(dma->runtime_code,fw.runtime_code.length) ||
        !dma_valid(dma->runtime_data,fw.runtime_data.length)) return IL_INVALID;
    int result = radio_enabled(io);
    if (result) return result;
    io->write32(io->context,CSR_INT_MASK,0); /* Polling, no IRQ handler installed. */
    io->write32(io->context,CSR_INT,0xFFFFFFFFu);
    io->write32(io->context,CSR_FH_INT_STATUS,0xFFFFFFFFu);
    io->write32(io->context,CSR_UCODE_DRV_GP1_CLR,
        CSR_UCODE_SW_BIT_RFKILL | CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);
    /* Repeat the RF-kill handshake as upstream does before starting BSM. */
    io->write32(io->context,CSR_UCODE_DRV_GP1_CLR,CSR_UCODE_SW_BIT_RFKILL);
    io->write32(io->context,CSR_UCODE_DRV_GP1_CLR,CSR_UCODE_SW_BIT_RFKILL);
    alive->stage = IL_BOOT_BSM;
    result = il3945_load_bsm(io,firmware,size,dma->init_code,dma->init_data);
    if (result) return result;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    io->write32(io->context,CSR_RESET,0);
    alive->stage = IL_BOOT_INIT_ALIVE;
    alive_wait_t wait = {alive,0,0,0};
    result = await_alive(io,rx,&wait);
    if (result) return result;
    io->delay_us(io->context,5000); /* Upstream RF-kill settling interval. */
    alive->stage = IL_BOOT_INIT_VERIFY;
    if ((result = radio_enabled(io)) || (result = verify_image(io,&fw.init_code))) return result;
    result = il_grab(io);
    if (result) return result;
    il_prph_write(io,BSM_DRAM_INST_PTR_REG,dma->runtime_code);
    il_prph_write(io,BSM_DRAM_DATA_PTR_REG,dma->runtime_data);
    il_prph_write(io,BSM_DRAM_DATA_BYTECOUNT_REG,fw.runtime_data.length);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    /* Load bit must be the last write, after all pointers/counts are visible. */
    il_prph_write(io,BSM_DRAM_INST_BYTECOUNT_REG,fw.runtime_code.length | BSM_DRAM_INST_LOAD);
    il_release(io);
    wait.stage = 1; wait.received = 0;
    alive->stage = IL_BOOT_RUNTIME_ALIVE;
    result = await_alive(io,rx,&wait);
    if (result) return result;
    io->delay_us(io->context,5000);
    if ((result = radio_enabled(io))) return result;
    alive->stage = IL_BOOT_RUNTIME_VERIFY;
    result = verify_image(io,&fw.runtime_code);
    if (result) return result;
    /* Runtime ALIVE precedes radio/thermal readiness (3945_alive_start).
     * Do not submit firmware commands while calibration is still starting. */
    alive->stage = IL_BOOT_CALIBRATION;
    result = il_grab(io);
    if (result) return result;
    uint32_t rfkill = il_prph_read(io,APMG_RFKILL_REG);
    il_release(io);
    if (rfkill == 0xffffffffu) return IL_ACCESS;
    if (!(rfkill & 1)) return IL_RFKILL;
    for (unsigned tick=0; tick<10000; tick++) {
        result = radio_enabled(io);
        if (result) return result;
        int32_t temperature=(int32_t)io->read32(io->context,CSR_UCODE_DRV_GP2);
        if (temperature) {
            if (temperature < -260 || temperature > 25) return IL_VERIFY;
            alive->stage = IL_BOOT_READY;
            return IL_OK;
        }
        io->delay_us(io->context,100);
    }
    return IL_TIMEOUT;
}

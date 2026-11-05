/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 * Native adaptation of common.c APM/EEPROM and 3945.c APM sequences.
 */
#include "internal.h"
int il_poll(const il_io_t* io, uint32_t reg, uint32_t mask,
             uint32_t bits, unsigned timeout_us) {
    for (unsigned elapsed = 0; elapsed < timeout_us; elapsed += 10) {
        uint32_t value = io->read32(io->context, reg);
        if (value == 0xFFFFFFFFu) return IL_ACCESS;
        if ((value & mask) == bits) return IL_OK;
        io->delay_us(io->context, 10);
    }
    return IL_TIMEOUT;
}
int il_update(const il_io_t* io, uint32_t reg, uint32_t clear, uint32_t set) {
    uint32_t value = io->read32(io->context, reg);
    if (value == 0xFFFFFFFFu) return IL_ACCESS;
    io->write32(io->context, reg, (value & ~clear) | set);
    return IL_OK;
}
int il_grab(const il_io_t* io) {
    int result = il_update(io, CSR_GP_CNTRL, 0, CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
    if (result) return result;
    result = il_poll(io, CSR_GP_CNTRL,
        CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY | CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP,
        CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY, 15000);
    if (result) il_release(io);
    return result;
}
void il_release(const il_io_t* io) {
    (void)il_update(io, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ, 0);
}
uint32_t il_prph_read(const il_io_t* io, uint32_t reg) {
    io->write32(io->context, HBUS_TARG_PRPH_RADDR, (reg & 0xFFFF) | (3u << 24));
    return io->read32(io->context, HBUS_TARG_PRPH_RDAT);
}
void il_prph_write(const il_io_t* io, uint32_t reg, uint32_t value) {
    io->write32(io->context, HBUS_TARG_PRPH_WADDR, (reg & 0xFFFF) | (3u << 24));
    io->write32(io->context, HBUS_TARG_PRPH_WDAT, value);
}
int il3945_apm_start(const il_io_t* io) {
    int result;
    if ((result = il_update(io, CSR_GIO_CHICKEN_BITS, 0,
            CSR_GIO_CHICKEN_BITS_REG_BIT_DIS_L0S_EXIT_TIMER |
            CSR_GIO_CHICKEN_BITS_REG_BIT_L1A_NO_L0S_RX)) ||
        (result = il_update(io, CSR_DBG_HPET_MEM_REG, 0, CSR_DBG_HPET_MEM_REG_VAL)) ||
        (result = il_update(io, CSR_HW_IF_CONFIG_REG, 0, CSR_HW_IF_CONFIG_REG_BIT_HAP_WAKE_L1A)) ||
        (result = il_update(io, CSR_ANA_PLL_CFG, 0, CSR39_ANA_PLL_CFG_VAL)) ||
        (result = il_update(io, CSR_GP_CNTRL, 0, CSR_GP_CNTRL_REG_FLAG_INIT_DONE))) return result;
    result = il_poll(io, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
                     CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY, 25000);
    if (result || (result = il_grab(io))) return result;
    il_prph_write(io, APMG_CLK_EN_REG, APMG_CLK_VAL_DMA_CLK_RQT | APMG_CLK_VAL_BSM_CLK_RQT);
    io->delay_us(io->context, 20);
    uint32_t value = il_prph_read(io, APMG_PCIDEV_STT_REG);
    if (value == 0xFFFFFFFFu) { result = IL_ACCESS; goto out; }
    il_prph_write(io, APMG_PCIDEV_STT_REG, value | APMG_PCIDEV_STT_VAL_L1_ACT_DIS);
    il_prph_write(io, APMG_RTC_INT_MSK_REG, 0);
    il_prph_write(io, APMG_RTC_INT_STT_REG, 0xFFFFFFFFu);
    value = il_prph_read(io, APMG_PS_CTRL_REG);
    if (value == 0xFFFFFFFFu) { result = IL_ACCESS; goto out; }
    il_prph_write(io, APMG_PS_CTRL_REG, value | APMG_PS_CTRL_VAL_RESET_REQ);
    io->delay_us(io->context, 5);
    il_prph_write(io, APMG_PS_CTRL_REG, value & ~APMG_PS_CTRL_VAL_RESET_REQ);
out:
    il_release(io);
    return result;
}
int il3945_apm_stop(const il_io_t* io) {
    int result = il_update(io, CSR_RESET, 0, CSR_RESET_REG_FLAG_STOP_MASTER);
    if (result) return result;
    result = il_poll(io, CSR_RESET, CSR_RESET_REG_FLAG_MASTER_DISABLED,
                     CSR_RESET_REG_FLAG_MASTER_DISABLED, 100);
    /* Reset even if stop-master acknowledgement timed out. No host DMA was
     * enabled by inspection, and buffers have never been posted to hardware. */
    int reset = il_update(io, CSR_RESET, 0, CSR_RESET_REG_FLAG_SW_RESET);
    if (reset) return reset;
    io->delay_us(io->context, 10);
    reset = il_update(io, CSR_GP_CNTRL,
        CSR_GP_CNTRL_REG_FLAG_INIT_DONE | CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ, 0);
    return result ? result : reset;
}
int il3945_inspect(const il_io_t* io, uint8_t bg_only, iwlegacy_info_t* info) {
    if (!il_io_valid(io) || !info) return IL_INVALID;
    info->eeprom = (il3945_eeprom_t){0};
    info->state = IL_ACCESS_FAILED;
    info->hardware_revision = io->read32(io->context, CSR_HW_REV);
    if (info->hardware_revision == 0xFFFFFFFFu) return info->error = IL_ACCESS;
    int result = il3945_apm_start(io);
    if (result) {
        info->state = result == IL_TIMEOUT ? IL_CLOCK_TIMEOUT : IL_ACCESS_FAILED;
        goto stopped;
    }
    info->state = IL_EEPROM_FAILED;
    uint32_t gp = io->read32(io->context, CSR_EEPROM_GP);
    if (gp == 0xFFFFFFFFu) { result = IL_ACCESS; goto stopped; }
    unsigned signature = gp & CSR_EEPROM_GP_VALID_MSK;
    if (signature != CSR_EEPROM_GP_GOOD_SIG_EEP_LESS_THAN_4K &&
        signature != CSR_EEPROM_GP_GOOD_SIG_EEP_MORE_THAN_4K) {
        result = IL_BAD_EEPROM; goto stopped;
    }
    io->write32(io->context, CSR_EEPROM_GP, gp & ~CSR_EEPROM_GP_IF_OWNER_MSK);
    uint8_t image[IL3945_EEPROM_SIZE];
    for (unsigned offset = 0; offset < sizeof(image); offset += 2) {
        io->write32(io->context, CSR_EEPROM_REG, (offset << 1) & CSR_EEPROM_REG_MSK_ADDR);
        result = il_poll(io, CSR_EEPROM_REG, CSR_EEPROM_REG_READ_VALID_MSK,
                         CSR_EEPROM_REG_READ_VALID_MSK, 5000);
        if (result) goto stopped;
        uint32_t value = io->read32(io->context, CSR_EEPROM_REG);
        if (value == 0xFFFFFFFFu) { result = IL_ACCESS; goto stopped; }
        image[offset] = value >> 16; image[offset + 1] = value >> 24;
    }
    if (!il3945_eeprom_parse(image, sizeof(image), bg_only, &info->eeprom)) {
        result = IL_BAD_EEPROM; goto stopped;
    }
    gp = io->read32(io->context, CSR_GP_CNTRL);
    if (gp == 0xFFFFFFFFu) { result = IL_ACCESS; goto stopped; }
    info->rfkill = !(gp & CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW);
    info->state = IL_EEPROM_READY;
stopped: {
    int stopped_result = il3945_apm_stop(io);
    if (!result && stopped_result) {
        result = stopped_result;
        info->state = IL_ACCESS_FAILED;
    }
    if (result) info->eeprom = (il3945_eeprom_t){0};
    info->error = result;
    return result;
}
}

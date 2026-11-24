/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 * Native adaptation of il3945_load_bsm / il3945_verify_bsm in 3945.c.
 */
#include "internal.h"
int il3945_load_bsm(const il_io_t* io, const uint8_t* firmware, uint32_t size,
                    uint32_t init_code_dma, uint32_t init_data_dma) {
    il3945_firmware_t fw;
    if (!il_io_valid(io) || !il3945_firmware_parse(firmware, size, &fw) ||
        !init_code_dma || !init_data_dma || (init_code_dma & 3) || (init_data_dma & 3) ||
        init_code_dma > 0xFFFFFFFFu - (fw.init_code.length - 1) ||
        init_data_dma > 0xFFFFFFFFu - (fw.init_data.length - 1)) return IL_INVALID;
    int result = il_grab(io);
    if (result) return result;
    il_prph_write(io, BSM_DRAM_INST_PTR_REG, init_code_dma);
    il_prph_write(io, BSM_DRAM_DATA_PTR_REG, init_data_dma);
    il_prph_write(io, BSM_DRAM_INST_BYTECOUNT_REG, fw.init_code.length);
    il_prph_write(io, BSM_DRAM_DATA_BYTECOUNT_REG, fw.init_data.length);
    for (uint32_t offset = 0; offset < fw.bootstrap.length; offset += 4)
        il_prph_write(io, BSM_SRAM_LOWER_BOUND + offset, il_le32(fw.bootstrap.data + offset));
    for (uint32_t offset = 0; offset < fw.bootstrap.length; offset += 4) {
        uint32_t word = il_prph_read(io, BSM_SRAM_LOWER_BOUND + offset);
        if (word != il_le32(fw.bootstrap.data + offset)) { result = IL_VERIFY; goto out; }
    }
    il_prph_write(io, BSM_WR_MEM_SRC_REG, 0);
    il_prph_write(io, BSM_WR_MEM_DST_REG, 0);
    il_prph_write(io, BSM_WR_DWCOUNT_REG, fw.bootstrap.length / 4);
    il_prph_write(io, BSM_WR_CTRL_REG, BSM_WR_CTRL_REG_BIT_START);
    result = IL_TIMEOUT;
    for (unsigned poll = 0; poll < 100; poll++) {
        uint32_t value = il_prph_read(io, BSM_WR_CTRL_REG);
        if (value == 0xFFFFFFFFu) { result = IL_ACCESS; break; }
        if (!(value & BSM_WR_CTRL_REG_BIT_START)) { result = IL_OK; break; }
        io->delay_us(io->context, 10);
    }
    if (!result) il_prph_write(io, BSM_WR_CTRL_REG, BSM_WR_CTRL_REG_BIT_START_EN);
    else il_prph_write(io, BSM_WR_CTRL_REG, 0);
out:
    il_release(io);
    return result;
}

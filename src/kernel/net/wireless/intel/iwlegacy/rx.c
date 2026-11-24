/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 * Native polled adaptation of 3945.c RX setup and 3945-mac.c RX restocking.
 */
#include "internal.h"
#include "rx.h"
#include "fh.h"
static int overlap(uint32_t a, uint32_t alen, uint32_t b, uint32_t blen) {
    return a <= b ? b - a < alen : a - b < blen;
}
int il3945_rx_prepare(il_rx_queue_t* q, volatile uint32_t* descriptors, uint32_t descriptors_dma,
                       volatile uint16_t* status, uint32_t status_dma,
                       const il_rx_buffer_t buffers[IL_RX_COUNT - 2]) {
    if (!q || q->active || !descriptors || !status || !buffers ||
        !descriptors_dma || (descriptors_dma & 255) || (status_dma & 15) || !status_dma ||
        descriptors_dma > 0xFFFFFC00u || status_dma > 0xFFFFFFF4u ||
        ((unsigned long)descriptors & 3) || ((unsigned long)status & 1) ||
        overlap(descriptors_dma, 1024, status_dma, 12)) return IL_INVALID;
    for (unsigned i = 0; i < IL_RX_COUNT - 2; i++) {
        uint32_t dma = buffers[i].dma;
        if (!buffers[i].data || !dma || (dma & 255) || dma > 0xFFFFF000u ||
            overlap(dma, IL_RX_BYTES, descriptors_dma, 1024) || overlap(dma, IL_RX_BYTES, status_dma, 12))
            return IL_INVALID;
        for (unsigned j = 0; j < i; j++)
            if (buffers[i].data == buffers[j].data || overlap(dma,IL_RX_BYTES,buffers[j].dma,IL_RX_BYTES))
                return IL_INVALID;
    }
    *q = (il_rx_queue_t){0};
    q->descriptors = descriptors; q->descriptors_dma = descriptors_dma;
    q->status = status; q->status_dma = status_dma;
    for (unsigned i = 0; i < 6; i++) status[i] = 0;
    for (unsigned i = 0; i < IL_RX_COUNT; i++) descriptors[i] = 0;
    for (unsigned i = 0; i < IL_RX_COUNT - 2; i++) {
        q->slots[i] = buffers[i]; descriptors[i] = buffers[i].dma;
    }
    q->write = IL_RX_COUNT - 2;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    return IL_OK;
}
int il3945_rx_start(const il_io_t* io, il_rx_queue_t* q) {
    if (!il_io_valid(io) || !q || q->active || !q->descriptors || !q->status || q->write != 254)
        return IL_INVALID;
    int result = il_grab(io);
    if (result) return result;
    io->write32(io->context, FH39_RCSR_RBD_BASE(0), q->descriptors_dma);
    io->write32(io->context, FH39_RCSR_RPTR_ADDR(0), q->status_dma);
    io->write32(io->context, FH39_RCSR_WPTR(0), 0);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    io->write32(io->context, FH39_RCSR_CONFIG(0),
        FH39_RCSR_RX_CONFIG_REG_VAL_DMA_CHNL_EN_ENABLE |
        FH39_RCSR_RX_CONFIG_REG_VAL_RDRBD_EN_ENABLE | FH39_RCSR_RX_CONFIG_REG_BIT_WR_STTS_EN |
        FH39_RCSR_RX_CONFIG_REG_VAL_MAX_FRAG_SIZE_128 |
        (8u << FH39_RCSR_RX_CONFIG_REG_POS_RBDC_SIZE) |
        FH39_RCSR_RX_CONFIG_REG_VAL_IRQ_DEST_INT_HOST |
        (1u << FH39_RCSR_RX_CONFIG_REG_POS_IRQ_RBTH));
    q->active = 1; q->published = q->write & ~7u;
    io->write32(io->context, FH39_RCSR_WPTR(0), q->published);
    (void)io->read32(io->context, FH39_RSSR_CTRL);
    il_release(io);
    return IL_OK;
}
int il3945_rx_poll(const il_io_t* io, il_rx_queue_t* q, il_rx_handler_t handler, void* context) {
    if (!il_io_valid(io) || !q || !q->active || q->polling) return IL_INVALID;
    unsigned closed = q->status[0] & 0xFFF;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (closed >= IL_RX_COUNT || ((closed - q->read) & 255) > ((q->published - q->read) & 255))
        return IL_VERIFY;
    unsigned processed = 0;
    q->polling = 1;
    while (q->read != closed) {
        unsigned slot = q->read;
        il_rx_buffer_t buffer = q->slots[slot];
        if (!buffer.data || q->slots[q->write].data) { q->polling = 0; return IL_VERIFY; }
        unsigned length = il_le32(buffer.data) & 0x3FFF;
        if (length < 4 || length > IL_RX_BYTES - 4) q->malformed++;
        else {
            if (handler) handler(context, buffer.data[4], buffer.data[5], il_le16(buffer.data + 6),
                                 buffer.data + 8, length - 4);
            q->packets++;
        }
        q->slots[slot] = (il_rx_buffer_t){0};
        q->slots[q->write] = buffer;
        q->descriptors[q->write] = buffer.dma;
        q->write = (q->write + 1) & 255;
        q->read = (q->read + 1) & 255;
        processed++;
    }
    int result = IL_OK;
    if (q->published != (q->write & ~7u)) {
        /* RX shares MAC access with command/TX owners. Do not drop their
         * wake request when returning receive buffers to the device. */
        uint32_t gp = io->read32(io->context,CSR_GP_CNTRL);
        unsigned held = gp & CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ;
        result = gp == 0xffffffffu ? IL_ACCESS : il_grab(io);
        if (!result) {
            __atomic_thread_fence(__ATOMIC_RELEASE);
            q->published = q->write & ~7u;
            io->write32(io->context, FH39_RCSR_WPTR(0), q->published);
            if (!held) il_release(io);
        }
    }
    q->polling = 0;
    return result ? result : (int)processed;
}
int il3945_rx_stop(const il_io_t* io, il_rx_queue_t* q) {
    if (!il_io_valid(io) || !q || q->polling) return IL_INVALID;
    if (!q->active) return IL_OK;
    int result = il_grab(io);
    if (result) return result;
    io->write32(io->context, FH39_RCSR_CONFIG(0), 0);
    result = il_poll(io, FH39_RSSR_STATUS, FH39_RSSR_CHNL0_RX_STATUS_CHNL_IDLE,
                     FH39_RSSR_CHNL0_RX_STATUS_CHNL_IDLE, 1000);
    il_release(io);
    if (!result) q->active = 0;
    return result;
}

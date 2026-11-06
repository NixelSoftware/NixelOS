/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 * Native single-inflight adaptation of Linux iwlegacy/common.c host commands
 * and 3945.c's 32-bit, four-buffer TFD layout.
 */
#include "internal.h"
#include "command.h"
#include "fh.h"
static void store16(volatile uint8_t* p, unsigned value) { p[0] = value; p[1] = value >> 8; }
static void store32(volatile uint8_t* p, uint32_t value) {
    p[0] = value; p[1] = value >> 8; p[2] = value >> 16; p[3] = value >> 24;
}
int il3945_command_prepare(il_command_queue_t* q, volatile void* descriptors,
                            uint8_t* buffer, uint32_t buffer_dma) {
    if (!q || q->pending || q->poisoned || !descriptors || ((unsigned long)descriptors & 255) || !buffer ||
        !buffer_dma || (buffer_dma & 3) || buffer_dma > 0xFFFFF000u) return IL_INVALID;
    *q = (il_command_queue_t){0};
    q->descriptors = descriptors; q->buffer = buffer; q->buffer_dma = buffer_dma;
    return IL_OK;
}
typedef struct {
    il_command_queue_t* queue; il_rx_handler_t events; void* context;
} command_dispatch_t;
static void dispatch(void* ctx, uint8_t id, uint8_t flags, uint16_t sequence,
                       const uint8_t* data, unsigned length) {
    command_dispatch_t* d = ctx; il_command_queue_t* q = d->queue;
    if (q->pending && !(sequence & 0x8000) && sequence == q->sequence) {
        if (id != q->command || (flags & 0x40)) q->result = IL_VERIFY;
        else {
            q->result = IL_OK; q->response_length = length;
            unsigned count = length < sizeof(q->response) ? length : sizeof(q->response);
            for (unsigned i = 0; i < count; i++) q->response[i] = data[i];
        }
        q->pending = 0; return;
    }
    if (d->events) d->events(d->context,id,flags,sequence,data,length);
}
int il3945_command_send(const il_io_t* io, il_rx_queue_t* rx, il_command_queue_t* q,
                         uint8_t command, const void* payload, unsigned length,
                         unsigned timeout_ms, il_rx_handler_t events, void* context) {
    if (!il_io_valid(io) || !rx || !rx->active || rx->polling || !q || !q->buffer ||
        !q->descriptors || q->pending || q->poisoned || length > 4092 ||
        (length && !payload) || !timeout_ms || timeout_ms > 30000) return IL_INVALID;
    uint32_t gp = io->read32(io->context,CSR_GP_CNTRL);
    if (gp == 0xFFFFFFFFu) return IL_ACCESS;
    if (!(gp & CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW)) return IL_RFKILL;
    int result = il_grab(io);
    if (result) return result;
    q->sequence = (uint16_t)(0x0400 | q->next | (length > 320 ? 0x4000 : 0));
    q->buffer[0] = command; q->buffer[1] = 0; store16(q->buffer + 2,q->sequence);
    for (unsigned i = 0; i < length; i++) q->buffer[4 + i] = ((const uint8_t*)payload)[i];
    unsigned padding = (0u - length) & 3u;
    for (unsigned i = 0; i < padding; i++) q->buffer[4 + length + i] = 0;
    volatile uint8_t* tfd = q->descriptors + q->next * 64;
    for (unsigned i = 0; i < 64; i++) tfd[i] = 0;
    store32(tfd,0x01000000u | (padding << 28));
    store32(tfd + 4,q->buffer_dma); store32(tfd + 8,length + 4);
    q->command = command; q->pending = 1; q->result = IL_TIMEOUT; q->response_length = 0;
    q->next = (q->next + 1) & 255;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    io->write32(io->context,HBUS_TARG_WRPTR,0x0400 | q->next);
    /* Flush the posted PCI doorbell before waiting. Keep MAC_ACCESS_REQ high
     * until completion: dropping it here permits sleep before the TFD fetch. */
    (void)io->read32(io->context,CSR_GP_CNTRL);
    command_dispatch_t d = {q,events,context};
    for (unsigned tick = 0; tick < timeout_ms * 10; tick++) {
        uint32_t irq = io->read32(io->context,CSR_INT);
        gp = io->read32(io->context,CSR_GP_CNTRL);
        if (irq == 0xFFFFFFFFu || gp == 0xFFFFFFFFu) { result = IL_ACCESS; goto failed; }
        if (!(gp & CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW)) { result = IL_RFKILL; goto failed; }
        if (irq & (CSR_INT_BIT_HW_ERR | CSR_INT_BIT_SW_ERR)) { result = IL_VERIFY; goto failed; }
        if (irq) io->write32(io->context,CSR_INT,irq);
        result = il3945_rx_poll(io,rx,dispatch,&d);
        if (result < 0) goto failed;
        if (!q->pending) { il_release(io); return q->result; }
        /* RX producer updates also grab/release NIC access; restore our wake
         * request if such an update occurred while consuming notifications. */
        result = il_update(io,CSR_GP_CNTRL,0,CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
        if (result) goto failed;
        io->delay_us(io->context,100);
    }
    result = IL_TIMEOUT;
failed:
    q->timeout_gp1 = io->read32(io->context,CSR_UCODE_DRV_GP1);
    q->timeout_tx_config = io->read32(io->context,FH39_TCSR_CONFIG(4));
    q->timeout_tx_status = io->read32(io->context,FH39_TSSR_TX_STATUS);
    il_release(io);
    q->poisoned = 1; /* Firmware may still own the posted command/TFD. */
    q->result = result;
    return result;
}

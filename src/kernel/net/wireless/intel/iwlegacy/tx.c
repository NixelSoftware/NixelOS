/* SPDX-License-Identifier: GPL-2.0-only
 * Native adaptation of Intel Linux iwlegacy 3945-mac.c TX/TFD format.
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 */
#include "internal.h"
#include "tx.h"
static void put16(volatile uint8_t* p, unsigned v) { p[0]=v; p[1]=v>>8; }
static void put32(volatile uint8_t* p, uint32_t v) { put16(p,v); put16(p+2,v>>16); }
int il3945_tx_prepare(il_tx_queue_t* q, volatile void* ring, uint8_t* data, uint32_t dma,
                       unsigned storage_bytes) {
    if (!q || q->pending || q->poisoned || !ring || ((unsigned long)ring & 255) ||
        !data || storage_bytes < IL3945_TX_STORAGE_BYTES || !dma || (dma & 3) ||
        dma > 0xffffffffu - (IL3945_TX_STORAGE_BYTES - 1)) return IL_INVALID;
    *q=(il_tx_queue_t){0}; q->descriptors=ring; q->buffer=data; q->dma=dma;
    return IL_OK;
}
int il3945_tx_submit(const il_io_t* io, il_tx_queue_t* q, const uint8_t* frame,
                       unsigned length, uint8_t station, uint8_t plcp) {
    if (!il_io_valid(io) || !q || !q->buffer || !q->descriptors || q->pending ||
        q->poisoned || !frame || length<24 || length>2304 || (station!=0 && station!=24)) return IL_INVALID;
    unsigned fc=il_le16(frame), type=fc & 12;
    if ((type!=0 && type!=8) || (fc & 0x0300)==0x0300 ||
        (fc & (0x0400|0x8000)) || (frame[22]&15)) return IL_INVALID;
    unsigned header=24 + ((type==8 && (fc&0x80)) ? 2 : 0);
    if (length<header) return IL_INVALID;
    uint32_t gp=io->read32(io->context,CSR_GP_CNTRL);
    if (gp==0xffffffffu) return IL_ACCESS;
    if (!(gp&CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW)) return IL_RFKILL;
    int result=il_grab(io); if (result) return result;
    /* Do not point every ring entry at the same mutable command/payload.
     * Keep completed slots intact until the producer wraps around the ring. */
    unsigned offset=q->next * IL3945_TX_SLOT_BYTES;
    uint8_t* b=q->buffer + offset;
    uint32_t dma=q->dma + offset;
    for (unsigned i=0;i<128;i++) b[i]=0;
    q->sequence=(uint16_t)((IL3945_TX_QUEUE<<8)|q->next);
    b[0]=0x1c; put16(b+2,q->sequence); put16(b+4,length);
    uint32_t flags=(frame[4]&1) ? 0 : (1u<<3);
    /* Linux requests firmware sequence control for all non-QoS frames.
     * CCMP authenticates only the fragment bits, not the sequence number. */
    if (type!=8 || !(fc&0x80)) flags|=1u<<13;
    /* ERP protection applies to OFDM transmissions, including null probes.
     * CCK frames already protect their own transmission in a mixed BSS. */
    if(q->cts_protection && (plcp!=10 && plcp!=20 && plcp!=55 && plcp!=110))
        flags|=(1u<<2)|(1u<<7);
    put32(b+8,flags); b[12]=plcp; b[13]=station;
    if (header==26) b[14]=frame[24]&15;
    put32(b+44,0xffffffffu); b[48]=0xff; b[49]=0x0f; b[50]=7; b[51]=15;
    if (!type) put16(b+52,(fc&0xf0)==0 ? 3 : 2);
    for (unsigned i=0;i<header;i++) b[56+i]=frame[i];
    unsigned first=(56+header+3)&~3u, second=length-header;
    for (unsigned i=0;i<second;i++) b[128+i]=frame[header+i];
    unsigned pad=(0u-second)&3;
    for (unsigned i=0;i<pad;i++) b[128+second+i]=0;
    volatile uint8_t* t=q->descriptors+q->next*64;
    for (unsigned i=0;i<64;i++) t[i]=0;
    put32(t,((second ? 2u : 1u)<<24)|(pad<<28));
    put32(t+4,dma); put32(t+8,first);
    if (second) { put32(t+12,dma+128); put32(t+16,second); }
    q->submitted=(il3945_tx_snapshot_t){
        .flags=flags, .tfd_control=((second?2u:1u)<<24)|(pad<<28),
        .first_length=first, .payload_length=second,
        .sequence=q->sequence, .frame_length=length, .frame_control=fc,
        .command_dma=dma, .payload_dma=second ? dma+128 : 0,
        .rate=plcp, .station=station, .security=b[15], .valid=1
    };
    q->pending=1; q->result=IL_TIMEOUT; q->next=(q->next+1)&255;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    io->write32(io->context,HBUS_TARG_WRPTR,(IL3945_TX_QUEUE<<8)|q->next);
    /* As for queue-4 commands, DMA may fetch after this function returns.
     * Keep MAC access requested until completion or failure cleanup. */
    (void)io->read32(io->context,CSR_GP_CNTRL);
    return IL_OK;
}
void il3945_tx_event(il_tx_queue_t* q, uint8_t id, uint16_t sequence,
                        const uint8_t* data, unsigned length) {
    if (!q || !q->pending || id!=0x1c || sequence!=q->sequence) return;
    if (!data || length<12) { q->poisoned=1; return; }
    q->submitted.status=il_le32(data+8); q->submitted.completed=1;
    unsigned status=q->submitted.status&255;
    q->result=(status==1 || status==2) ? IL_OK : IL_VERIFY;
    q->pending=0;
}

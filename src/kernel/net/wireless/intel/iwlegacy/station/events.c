/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 * Native Intel 3945 station lifecycle; see ../README.md for module ownership.
 */
#include "private.h"

void il_station_firmware_error(void* context,uint8_t id,uint8_t flags,uint16_t seq,
                           const uint8_t* data,unsigned length) {
    il_station_t* s=context; (void)flags; (void)seq;
    if(id!=2) return;
    s->error=IL_VERIFY;
    if(length<20 || s->error_reply.valid) return;
    s->error_reply.type=il_le32(data);
    s->error_reply.command=data[4]; s->error_reply.sequence=il_le16(data+6);
    s->error_reply.info=il_le32(data+8); s->error_reply.valid=1;
}

void il_station_event(void* context,uint8_t id,uint8_t flags,uint16_t seq,const uint8_t* data,unsigned length) {
    il_station_t* s=context; (void)flags;
    if (id==2) { il_station_firmware_error(s,id,flags,seq,data,length); return; }
    if (id==0x1c) {
        unsigned pending=s->tx.pending;
        il3945_tx_event(&s->tx,id,seq,data,length);
        if (pending && !s->tx.pending) {
            unsigned last=(s->tx_history_next+7)&7;
            if(s->tx_history_count && s->tx_history[last].sequence==seq)
                s->tx_history[last]=s->tx.submitted;
            il_release(&s->io);
            s->diagnostics.last_tx_status=il_le32(data+8)&0xffff;
            s->diagnostics.last_tx_misc=il_le32(data+8);
            s->diagnostics.tx_completed++;
            if(!s->tx.result && (s->diagnostics.last_tx_status&255)==1 &&
               (s->tx.submitted.flags&(1u<<3))) {
                s->last_ack=s->now_ms; s->probing=0; s->probe_attempts=0;
            }
            s->probe_pending=0;
            if(s->tx.result) s->diagnostics.tx_errors++;
        }
        return;
    }
    if(id!=0x1b) return;
    if(length<32 || data[0]>20 || length<32u+data[0]) { s->diagnostics.rx_invalid++; return; }
    unsigned h=8+data[0], n=il_le16(data+h+6);
    if(n<24 || n>2304 || n>length-h-24 ||
       il_le16(data+h)!=s->ap.channel || ((il_le16(data+h+2)&1)?2:5)!=s->ap.band) return;
    const uint8_t* f=data+h+8;
    if((il_le32(f+n)&3)!=3) { s->diagnostics.rx_errors++; return; }
    if(s->count==8) { s->diagnostics.rx_errors++; return; }
    unsigned i=(s->head+s->count)&7; copy(s->frames[i],f,n); s->lengths[i]=n; s->count++;
}


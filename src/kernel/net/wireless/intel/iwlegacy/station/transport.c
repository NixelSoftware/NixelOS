/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 */
#include "private.h"

int il_station_pump(il_station_t* s) {
    if (s->tx.pending) { int r=il_grab(&s->io); if(r) return r; }
    uint32_t gp=s->io.read32(s->io.context,CSR_GP_CNTRL),irq=s->io.read32(s->io.context,CSR_INT);
    if(gp==0xffffffffu || irq==0xffffffffu) return IL_ACCESS;
    if(!(gp&CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW)) return IL_RFKILL;
    if(irq&(CSR_INT_BIT_HW_ERR|CSR_INT_BIT_SW_ERR)) {
        /* Consume only error notifications already completed by DMA. Do not
         * dispatch network traffic or new TX work after a firmware fault. */
        if((irq&CSR_INT_BIT_SW_ERR) && !(irq&CSR_INT_BIT_HW_ERR)) {
            /* The interrupt may precede the final RX producer DMA write. */
            for(unsigned tick=0;tick<10 && !s->error_reply.valid;tick++) {
                if(il3945_rx_poll(&s->io,&s->rx,il_station_firmware_error,s)<0) break;
                s->io.delay_us(s->io.context,100);
            }
        }
        return IL_VERIFY;
    }
    int ack=il_ack_interrupts(&s->io,irq);
    if(ack) return ack;
    int r=il3945_rx_poll(&s->io,&s->rx,il_station_event,s);
    if(r>=0 && s->tx.pending && !s->tx.poisoned) {
        int wake=il_grab(&s->io); if(wake) return wake;
    }
    return r<0?r:(s->tx.poisoned ? IL_VERIFY : s->error);
}
int il_station_wait_tx(il_station_t* s) {
    for(unsigned tick=0;s->tx.pending && tick<20000;tick++) {
        int r=il_station_pump(s); if(r) return r;
        s->io.delay_us(s->io.context,100);
    }
    if(s->tx.pending) { s->tx.poisoned=1; return IL_TIMEOUT; }
    return s->tx.poisoned ? IL_VERIFY : s->error;
}

int il_station_command(il_station_t* s,uint8_t id,const void* data,unsigned size) {
    /* Association replies can arrive before the matching host TX completion.
     * Finish that TX before changing RXON, timing, rates or radio power. */
    int r=il_station_wait_tx(s);
    if(!r) r=il3945_command_send(&s->io,&s->rx,&s->commands,id,data,size,1000,il_station_event,s);
    if(r) s->error=r;
    return r;
}

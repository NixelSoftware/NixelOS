#include "station_fixture.h"

int main(void) {
    station_model_t m; il3945_eeprom_t e; il_network_t ap;
    il_tx_queue_t invalid={0};
    assert(il3945_tx_prepare(&invalid,tx_ring,tx_storage,0x19000,4096)==IL_INVALID);
    assert(il3945_tx_prepare(&invalid,tx_ring,tx_storage,0xfffff000u,sizeof(tx_storage))==IL_INVALID);
    station_setup(&m,&e,&ap);
    assert(!il3945_station_connect(&native,&e,&ap,0,0,0,0));
    assert(native.active && native.aid==1 && m.transmitted==2 && m.commands==14 && m.station_adds==2 && m.stations==((1u<<24)|1u));
    station_setup(&m,&e,&ap); m.mode=7;
    assert(!il3945_station_connect(&native,&e,&ap,0,0,0,0));
    assert(native.active && !native.tx.pending && !m.deferred_tx && m.commands==14);
    uint8_t eth[64]={0}; memcpy(eth,ap.bssid,6); memcpy(eth+6,e.mac,6); eth[12]=8;
    for(unsigned i=0;i<4300;i++) {
        /* Keep the previous descriptor and all of its device-visible bytes
         * unchanged, even after completion and the next producer doorbell. */
        uint8_t previous[IL3945_TX_SLOT_BYTES], descriptor[64];
        uint8_t* previous_data=tx_data;
        unsigned previous_slot=native.tx.sequence & 255;
        memcpy(previous,previous_data,sizeof(previous));
        memcpy(descriptor,tx_ring+previous_slot*64,sizeof(descriptor));
        eth[63]=(uint8_t)i;
        assert(!il3945_station_send(&native,eth,sizeof(eth)));
        assert(tx_data!=previous_data && !memcmp(previous,previous_data,sizeof(previous)));
        assert(!memcmp(descriptor,tx_ring+previous_slot*64,sizeof(descriptor)));
        assert(il3945_station_send(&native,eth,sizeof(eth))==IL_ACCESS);
        assert(!il3945_station_poll(&native,1000+i));
    }
    assert(native.tx_history_count==8);
    unsigned last=(native.tx_history_next+7)&7;
    assert(native.tx_history[last].sequence==native.tx.sequence);
    assert(native.tx_history[last].completed && native.tx_history[last].ether_type==0x800);
    assert(native.tx_history[last].command_dma==il_le32(tx_ring+64*(native.tx.sequence&255)+4));
    assert(native.tx.next==206 && native.diagnostics.tx_completed==4302);
    uint8_t frame[128],payload[200]={0}; unsigned n;
    assert(!wifi_ethernet_encapsulate(e.mac,ap.bssid,1,eth,sizeof(eth),frame,sizeof(frame),&n));
    frame[1]=2; memcpy(frame+4,e.mac,6); memcpy(frame+10,ap.bssid,6); memcpy(frame+16,ap.bssid,6);
    payload[8]=1; payload[10]=1; payload[14]=n; memcpy(payload+16,frame,n); put32(payload+16+n,3);
    post(&m,0x1b,0x8000,payload,n+32); native.receive=receive_frame;
    assert(!il3945_station_poll(&native,6000) && received==1);
    native.io.write32(native.io.context,CSR_GP_CNTRL,0);
    assert(il3945_station_poll(&native,6001)==IL_RFKILL && !native.active);
    station_setup(&m,&e,&ap); e.channels[0].flags=1;
    assert(il3945_station_connect(&native,&e,&ap,0,0,0,0)==IL_INVALID && !m.transmitted);
    station_setup(&m,&e,&ap); m.mode=4;
    assert(il3945_station_connect(&native,&e,&ap,0,0,0,0)==IL_VERIFY);
    assert(!native.active && !m.transmitted && native.commands.command==0x18);
    assert(!native.commands.pending && !native.commands.poisoned && native.commands.response[0]==2);
    station_setup(&m,&e,&ap); m.mode=1;
    assert(il3945_station_connect(&native,&e,&ap,0,0,0,0)==IL_TIMEOUT && !native.active);
    station_setup(&m,&e,&ap); assert(!il3945_station_connect(&native,&e,&ap,0,0,0,0));
    m.mode=5;
    assert(!il3945_station_send(&native,eth,sizeof(eth)));
    assert(m.t.m.regs[CSR_GP_CNTRL/4] & CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
    assert(!il3945_station_poll(&native,native.now_ms+1));
    assert(!native.tx.pending && !(m.t.m.regs[CSR_GP_CNTRL/4] & CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ));
    m.mode=2; assert(!il3945_station_send(&native,eth,sizeof(eth)));
    assert(il3945_station_poll(&native,native.tx_started+2000)==IL_TIMEOUT && native.tx.poisoned && native.tx.pending);
    station_setup(&m,&e,&ap); m.secure=1;
    ap.security=IL_SECURITY_WPA2_PSK; ap.capability|=16;
    memcpy(ap.rsn,ap_rsn,sizeof(ap_rsn)); ap.rsn_length=sizeof(ap_rsn);
    assert(!il3945_station_connect(&native,&e,&ap,(const uint8_t*)"password",8,fixed_nonce,0));
    assert(native.active && native.security.authorized && m.eapol==2 && m.station_adds==2);
    assert(!memcmp(native.security.ptk,ap_ptk,64));
    assert(!il3945_station_send(&native,eth,sizeof(eth)) && (tx_data[57]&0x40));
    assert(native.security.pairwise.next_tx_pn==2);
    /* Hardware may rewrite non-QoS sequence numbers after software CCMP.
     * The AP must still authenticate/decrypt the transmitted packet. */
    uint8_t on_air[256], verified[256]; unsigned verified_length;
    unsigned on_air_length=il_le16(tx_data+4);
    memcpy(on_air,tx_data+56,24); memcpy(on_air+24,tx_data+128,on_air_length-24);
    on_air[22]=0x70; on_air[23]=0x42;
    wifi_ccmp_key_t tx_check={0};
    assert(!wifi_ccmp_install(&tx_check,ap_ptk+32,0,ap.bssid,e.mac));
    assert(!wifi_ccmp_decrypt(&tx_check,on_air,on_air_length,verified,sizeof(verified),&verified_length));
    assert(!il3945_station_poll(&native,1000));
    /* Full MTU software-decrypted RX must pass through the native event queue,
     * while replayed or unauthenticated frames never reach the Ethernet stack. */
    wifi_ccmp_key_t ap_key={0};
    assert(!wifi_ccmp_install(&ap_key,ap_ptk+32,0,ap.bssid,e.mac));
    uint8_t plain[1600]={8,2}, encrypted[1632], reception[1680]={0};
    memcpy(plain+4,e.mac,6); memcpy(plain+10,ap.bssid,6); memcpy(plain+16,ap.bssid,6);
    const uint8_t snap[]={0xaa,0xaa,3,0,0,0,8,0}; memcpy(plain+24,snap,8);
    memset(plain+32,0x5a,1504);
    unsigned encrypted_length;
    assert(!wifi_ccmp_encrypt(&ap_key,plain,1536,encrypted,sizeof(encrypted),&encrypted_length));
    reception[8]=1; reception[10]=1; reception[14]=encrypted_length; reception[15]=encrypted_length>>8;
    memcpy(reception+16,encrypted,encrypted_length); put32(reception+16+encrypted_length,3);
    native.receive=receive_frame; expected_length=1518;
    post(&m,0x1b,0x8000,reception,encrypted_length+32);
    assert(!il3945_station_poll(&native,1001) && received==2);
    post(&m,0x1b,0x8000,reception,encrypted_length+32);
    assert(!il3945_station_poll(&native,1002) && received==2);
    il3945_station_leave(&native);
    assert(!native.active && !native.security.initialized && !native.security.pairwise.active);
    station_setup(&m,&e,&ap); assert(!il3945_station_connect(&native,&e,&ap,0,0,0,0));
    uint8_t fault[20]={0}; put32(fault,5); fault[4]=0x1c;
    fault[6]=native.tx.sequence; fault[7]=native.tx.sequence>>8;
    put32(fault+8,0x00740000);
    post(&m,2,0x8000,fault,sizeof(fault));
    uint8_t later_fault[20]; memcpy(later_fault,fault,sizeof(later_fault));
    later_fault[6]^=1; post(&m,2,0x8000,later_fault,sizeof(later_fault));
    tx_data[0]=0; /* Diagnostics must survive DMA-buffer reset/corruption. */
    native.error_log_base=0x800100; m.t.mode=14;
    m.t.m.regs[CSR_INT/4]=CSR_INT_BIT_SW_ERR;
    assert(il3945_station_poll(&native,native.now_ms+1)==IL_VERIFY);
    assert(native.runtime_irq==CSR_INT_BIT_SW_ERR && native.runtime_error==IL_VERIFY);
    assert(native.error_reply.valid && native.error_reply.command==0x1c);
    assert(native.error_reply.type==5 && native.error_reply.info==0x00740000);
    assert(native.error_reply.sequence==native.tx.sequence);
    assert(native.last_data_tx.valid && native.last_data_tx.sequence==native.tx.sequence);
    assert(native.last_data_tx.frame_length==il_le16(tx_data+4));
    assert(native.last_data_tx.first_length==80 && native.last_data_tx.station==0);

    assert(!native.firmware_error.read_error && native.firmware_error.count==1);
    assert(native.firmware_error.entry[0]==5 && native.firmware_error.entry[2]==0x1111 && native.firmware_error.entry[6]==77);
    assert(!(m.t.m.regs[CSR_GP_CNTRL/4]&CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ));
    il3945_error_log_t bad;
    unsigned before=m.t.memory_offset;
    assert(il3945_read_error_log(&native.io,0x807ff0,&bad)==IL_INVALID);
    assert(il3945_read_error_log(&native.io,0x800101,&bad)==IL_INVALID);
    assert(m.t.memory_offset==before);
    m.error_count=0xffffffffu;
    assert(il3945_read_error_log(&native.io,0x800100,&bad)==IL_VERIFY);
    assert(m.t.memory_offset==0x800104);
    m.error_count=0;
    assert(!il3945_read_error_log(&native.io,0x800100,&bad) && !bad.count);
    before=m.t.memory_offset;
    m.t.m.regs[CSR_UCODE_DRV_GP1/4]=CSR_UCODE_DRV_GP1_BIT_MAC_SLEEP;
    assert(il3945_read_error_log(&native.io,0x800100,&bad)==IL_ACCESS);
    assert(m.t.memory_offset==before);
    station_setup(&m,&e,&ap); assert(!il3945_station_connect(&native,&e,&ap,0,0,0,0));
    unsigned idle=native.now_ms+30000;
    m.mode=3; /* AP no longer ACKs; two failed probes precede link loss. */
    assert(!il3945_station_poll(&native,idle) && native.probing);
    assert(!il3945_station_poll(&native,idle+500));
    assert(il3945_station_poll(&native,idle+1000)==IL_TIMEOUT);
    assert(!native.runtime_irq && native.runtime_error==IL_TIMEOUT);
    station_setup(&m,&e,&ap); assert(!il3945_station_connect(&native,&e,&ap,0,0,0,0));
    m.mode=6; assert(!il3945_station_send(&native,eth,sizeof(eth)));
    assert(il3945_station_poll(&native,native.now_ms+1)==IL_VERIFY);
    assert(!native.runtime_irq && native.tx.pending && native.tx.poisoned);
    assert(!(m.t.m.regs[CSR_GP_CNTRL/4]&CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ));
    station_setup(&m,&e,&ap); assert(!il3945_station_connect(&native,&e,&ap,0,0,0,0));
    m.mode=8; m.t.m.regs[CSR_INT/4]=CSR_INT_BIT_SW_ERR;
    assert(il3945_station_poll(&native,native.now_ms+1)==IL_VERIFY);
    assert(native.error_reply.valid && native.error_reply.command==0x1c);
    station_setup(&m,&e,&ap); assert(!il3945_station_connect(&native,&e,&ap,0,0,0,0));
    native.last_rx=60000; native.temperature=-90; m.mode=9;
    assert(il3945_station_poll(&native,60000)==IL_VERIFY);
    assert(native.commands.pending && native.commands.poisoned);
    assert(native.error_reply.valid && native.error_reply.type==5);
    puts("iwlegacy station: association, split TFDs, 4300-frame wrap, RX, regulatory refusal, RF-kill, retained TX timeout, WPA2 M1-M4 and CCMP TX passed");
    return 0;
}

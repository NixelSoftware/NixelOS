/* Firmware/register model exercising the real MLME, command and DMA TX paths. */
#define IL_TRANSPORT_FIXTURE_ONLY
#include "transport_test.c"
static uint8_t tfds[16384] __attribute__((aligned(256)));
static uint8_t cmd_buffer[4096] __attribute__((aligned(4096)));
#include "../station.h"
#include "../../../ieee80211.h"
#include "../../../security/wpa2.h"
#include "station_vectors.h"
static uint8_t tx_ring[16384] __attribute__((aligned(256)));
static uint8_t tx_data[4096] __attribute__((aligned(4096)));
typedef struct { transport_t t; il_station_t* station; unsigned mode, commands, transmitted, secure, eapol, stations, station_adds, error_count, deferred_tx; } station_model_t;
static void post(station_model_t* m,uint8_t id,uint16_t seq,const uint8_t* data,unsigned n) {
    unsigned idx=status[0]&255;
    uint8_t* p=m->station->rx.slots[idx].data;
    put32(p,n+4); p[4]=id; p[5]=0; p[6]=seq; p[7]=seq>>8; memcpy(p+8,data,n);
    status[0]=(idx+1)&255;
}
static void ap_eapol(station_model_t* m,const uint8_t* key,unsigned length) {
    uint8_t p[800]={0},*f=p+16;
    p[8]=1; p[10]=1; unsigned n=32+length; p[14]=n; p[15]=n>>8;
    f[0]=8; f[1]=2; memcpy(f+4,m->station->eeprom.mac,6);
    memcpy(f+10,m->station->ap.bssid,6); memcpy(f+16,m->station->ap.bssid,6);
    const uint8_t snap[]={0xaa,0xaa,3,0,0,0,0x88,0x8e}; memcpy(f+24,snap,8); memcpy(f+32,key,length);
    put32(f+n,3); post(m,0x1b,0x8000,p,n+32);
}
static uint32_t station_read(void* context,uint32_t reg) {
    station_model_t* m=context;
    if(reg==CSR_INT && m->deferred_tx && !--m->deferred_tx) {
        uint8_t done[12]={0}; done[8]=1;
        post(m,0x1c,m->station->tx.sequence,done,sizeof(done));
    }
    if(reg==HBUS_TARG_MEM_RDAT && m->t.mode==14) {
        static const uint32_t words[8]={1,5,1234,0x1111,0x2222,0x3333,0x4444,77};
        unsigned idx=(m->t.memory_offset-0x800100)/4;
        assert(idx<8); m->t.memory_offset+=4; return idx ? words[idx] : m->error_count;
    }
    if(reg==CSR_INT && m->mode==5 && m->station->tx.pending) {
        assert(m->t.m.regs[CSR_GP_CNTRL/4] & CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
        uint8_t done[12]={0}; done[8]=1;
        post(m,0x1c,m->station->tx.sequence,done,sizeof(done)); m->mode=0;
    }
    if(reg==CSR_UCODE_DRV_GP2) return (uint32_t)-100;
    return transport_read(&m->t,reg);
}
static void station_write(void* context,uint32_t reg,uint32_t value) {
    station_model_t* m=context;
    if(reg==HBUS_TARG_MEM_RADDR && m->t.mode==14) {
        assert(value==0x800100); m->t.memory_offset=value; return;
    }
    transport_write(&m->t,reg,value);
    if(reg!=HBUS_TARG_WRPTR) return;
    if(value&0x400) {
        assert(!m->station->tx.pending); /* No radio reconfiguration during TX. */
        m->commands++;
        if(m->mode==9) {
            m->t.m.regs[CSR_INT/4]=CSR_INT_BIT_SW_ERR; m->mode=8;
            return;
        }
        if (cmd_buffer[0]==0x77) assert(cmd_buffer[4]==(m->station->awake_flags & 8));
        uint8_t answer[4]={1}; unsigned n=4; /* firmware status may be DMA-padded */
        if (cmd_buffer[0]==0x10 && !(cmd_buffer[4+36]&32)) m->stations=0;
        if (cmd_buffer[0]==0x18) {
            unsigned id=cmd_buffer[4+12]; assert(id==0 || id==24);
            assert(cmd_buffer[4+59]==0xc0);
            m->station_adds++;
            if (m->mode==4 || (m->stations & (1u<<id))) answer[0]=2; /* Duplicate firmware entry. */
            else m->stations |= 1u<<id;
        }
        post(m,cmd_buffer[0],il_le16(cmd_buffer+2),answer,n);
        if(m->secure && cmd_buffer[0]==0x10 && (cmd_buffer[4+36]&32)) ap_eapol(m,ap_m1,sizeof(ap_m1));
        return;
    }
    m->transmitted++;
    unsigned idx=(value-1)&255; uint8_t* t=tx_ring+64*idx;
    unsigned n=il_le16(tx_data+4), h=((tx_data[56]&12)==8 && (tx_data[56]&0x80))?26:24;
    assert((il_le32(t)>>24&15)==(n>h?2:1));
    assert(il_le32(t+4)==0x19000 && il_le32(t+8)==((56+h+3)&~3u));
    assert(il_le32(t+12)==0x19080 && il_le32(t+16)==n-h);
    assert(tx_data[12]==10 && !tx_data[13] && !tx_data[15]);
    assert((il_le32(tx_data+8)&(1u<<13)) == (h==24 ? 1u<<13 : 0));
    if(m->mode==2 || m->mode==5) return; /* Firmware retains ownership indefinitely. */
    uint8_t done[12]={0}; done[8]=m->mode==3?0x83:1;
    if(m->mode==7 && tx_data[56]==0) m->deferred_tx=2;
    else post(m,0x1c,il_le16(tx_data+2),done,m->mode==6?8:sizeof(done));
    if(m->secure && (tx_data[56]&12)==8 && tx_data[134]==0x88 && tx_data[135]==0x8e) {
        unsigned keylen=n-h-8; const uint8_t* key=tx_data+136;
        assert(wifi_wpa2_eapol_verify(ap_ptk,key,keylen)==1);
        m->eapol++;
        if(key[5]==1 && key[6]==0x0a) ap_eapol(m,ap_m3,sizeof(ap_m3));
        else assert(key[5]==3 && key[6]==0x0a);
    }
    if((tx_data[56]&12)==0 && (!m->mode || m->mode==7)) {
        uint8_t p[128]={0}; uint8_t* f=p+16;
        p[8]=1; p[10]=1;
        f[0]=tx_data[56]==0xb0?0xb0:0x10;
        memcpy(f+4,m->station->eeprom.mac,6); memcpy(f+10,m->station->ap.bssid,6); memcpy(f+16,m->station->ap.bssid,6);
        if(f[0]==0xb0) f[26]=2; else { f[24]=1; f[28]=1; f[29]=0xc0; }
        p[14]=30; put32(f+30,3); post(m,0x1b,0x8000,p,62);
    }
}
static void station_delay(void* context,uint32_t us) {
    station_model_t* m=context;
    transport_delay(&m->t,us);
    if(m->mode==8) {
        uint8_t fault[20]={0}; put32(fault,5); fault[4]=0x1c;
        post(m,2,0x8000,fault,sizeof(fault)); m->mode=0;
    }
}
static il_station_t native;
static void station_setup(station_model_t* m,il3945_eeprom_t* e,il_network_t* ap) {
    memset(m,0,sizeof(*m)); memset(&native,0,sizeof(native)); memset(e,0,sizeof(*e)); memset(ap,0,sizeof(*ap));
    transport_setup(&m->t); m->station=&native; native.rx=m->t.rx;
    native.io=(il_io_t){m,station_read,station_write,station_delay};
    native.awake_flags=8; m->error_count=1;
    assert(!il3945_rx_start(&native.io,&native.rx));
    assert(!il3945_command_prepare(&native.commands,tfds,cmd_buffer,0x18000));
    assert(!il3945_tx_prepare(&native.tx,tx_ring,tx_data,0x19000));
    e->mac[0]=2; e->mac[5]=1; e->channels[0]=(il3945_channel_t){1,2,9,17};
    for(unsigned g=0;g<5;g++) {
        uint8_t* group=e->calibration+64*g;
        for(unsigned i=0;i<5;i++) { group[4*i]=10+i*10; group[4*i+1]=40-i*10; }
        group[60]=40; group[61]=40+g*20; group[62]=(uint8_t)-100; group[63]=255;
    }
    ap->bssid[0]=2; ap->bssid[5]=2; ap->ssid_length=4; memcpy(ap->ssid,"test",4);
    ap->band=2; ap->channel=1; ap->capability=1; ap->beacon_interval=100; ap->rates[0]=0x82; ap->rate_count=1;
}
static unsigned received, expected_length=64;
static uint8_t receive_frame(const uint8_t* p,uint32_t n) {
    assert(n==expected_length && !memcmp(p,native.eeprom.mac,6)); received++; return 1;
}
static int fixed_nonce(void* ctx,uint8_t* out,unsigned n) {
    (void)ctx; assert(n==32); for(unsigned i=0;i<n;i++) out[i]=64+i; return 0;
}
int main(void) {
    station_model_t m; il3945_eeprom_t e; il_network_t ap;
    station_setup(&m,&e,&ap);
    assert(!il3945_station_connect(&native,&e,&ap,0,0,0,0));
    assert(native.active && native.aid==1 && m.transmitted==2 && m.commands==14 && m.station_adds==2 && m.stations==((1u<<24)|1u));
    station_setup(&m,&e,&ap); m.mode=7;
    assert(!il3945_station_connect(&native,&e,&ap,0,0,0,0));
    assert(native.active && !native.tx.pending && !m.deferred_tx && m.commands==14);
    uint8_t eth[64]={0}; memcpy(eth,ap.bssid,6); memcpy(eth+6,e.mac,6); eth[12]=8;
    for(unsigned i=0;i<4300;i++) {
        assert(!il3945_station_send(&native,eth,sizeof(eth)));
        assert(il3945_station_send(&native,eth,sizeof(eth))==IL_ACCESS);
        assert(!il3945_station_poll(&native,1000+i));
    }
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
    assert(il3945_station_poll(&native,native.now_ms+30000)==IL_TIMEOUT);
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

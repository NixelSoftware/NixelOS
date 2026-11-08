#include "station_fixture.h"
static void receive_data(station_model_t* m,uint16_t sequence,int retry,int tid) {
    uint8_t f[66]={8,2},p[128]={0};
    unsigned h=tid<0?24:26,n=h+40;
    if(tid>=0) { f[0]=0x88; f[24]=tid; }
    memcpy(f+4,native.eeprom.mac,6); memcpy(f+10,native.ap.bssid,6); memcpy(f+16,native.ap.bssid,6);
    f[1]|=retry?8:0; f[22]=sequence; f[23]=sequence>>8;
    const uint8_t snap[]={0xaa,0xaa,3,0,0,0,8,0}; memcpy(f+h,snap,8);
    p[8]=native.ap.channel; p[10]=1; p[14]=n;
    memcpy(p+16,f,n); put32(p+16+n,3); post(m,0x1b,0x8000,p,n+32);
}
int main(void) {
    station_model_t m; il3945_eeprom_t e; il_network_t ap;
    station_setup(&m,&e,&ap); ap.rates[1]=12; ap.rate_count=2;
    m.assoc_ies[0]=42; m.assoc_ies[1]=1; m.assoc_ies[2]=6; m.assoc_ie_length=3;
    assert(!il3945_station_connect(&native,&e,&ap,0,0,0,0));
    assert(native.ap.erp_valid && native.ap.erp==6);
    /* Non-WMM configuration is observable in the submitted firmware command. */
    assert(IL3945_TX_QUEUE==2 && (native.tx.sequence>>8)==2);
    native.receive=receive_frame; expected_length=46; received=0;
    receive_data(&m,0x120,0,-1); assert(!il3945_station_poll(&native,100) && received==1);
    unsigned valid_at=native.last_rx;
    receive_data(&m,0x120,1,-1); assert(!il3945_station_poll(&native,101) && received==1);
    assert(native.last_rx==valid_at);
    receive_data(&m,0x130,1,-1); assert(!il3945_station_poll(&native,102) && received==2);
    receive_data(&m,0x130,1,0); assert(!il3945_station_poll(&native,102) && received==3);
    receive_data(&m,0x130,1,1); assert(!il3945_station_poll(&native,102) && received==4);
    receive_data(&m,0x130,1,1); assert(!il3945_station_poll(&native,102) && received==4);
    uint8_t eth[64]={0}; memcpy(eth+6,e.mac,6); eth[12]=8;
    native.tx_plcp=13; /* Exercise OFDM protection independently of fixed rate. */
    assert(!il3945_station_send(&native,eth,sizeof(eth)));
    assert((native.tx.submitted.flags&0x84)==0x84);
    assert(!il3945_station_poll(&native,103));
    native.tx_plcp=10;
    assert(!il3945_station_send(&native,eth,sizeof(eth)));
    assert(!(native.tx.submitted.flags&0x84));
    assert(!il3945_station_poll(&native,104));
    /* Reconnect clears duplicate history. */
    il3945_station_clear(&native); assert(!native.rx_sequence_valid);
    station_setup(&m,&e,&ap);
    m.assoc_ies[0]=42; m.assoc_ies[1]=2; m.assoc_ies[2]=0; m.assoc_ie_length=3;
    assert(il3945_station_connect(&native,&e,&ap,0,0,0,0)==IL_VERIFY && !native.active);
    assert(native.connection_reject==IL_REJECT_ASSOC_IES);
    assert(native.failure_phase==IL_STATION_ASSOC_WAIT && native.runtime_error==IL_VERIFY);
    assert(!native.runtime_irq && !native.runtime_fh);
    station_setup(&m,&e,&ap); m.response_status=17;
    assert(il3945_station_connect(&native,&e,&ap,0,0,0,0)==IL_VERIFY);
    assert(native.management_status==17 && native.connection_reject==IL_REJECT_ASSOC_STATUS);
    station_setup(&m,&e,&ap); assert(!il3945_station_connect(&native,&e,&ap,0,0,0,0));
    unsigned now=native.now_ms+30000;
    assert(!il3945_station_poll(&native,now) && native.probe_pending);
    assert(tx_data[56]==0x48 && il_le16(tx_data+4)==24);
    assert(!il3945_station_poll(&native,now+1) && !native.probing && native.last_ack==now+1);
    /* Valid beacons are distinct from data/ACK activity. */
    uint8_t beacon[80]={0}; beacon[8]=1; beacon[10]=1; beacon[14]=44;
    uint8_t* f=beacon+16; f[0]=0x80;
    memset(f+4,255,6); memcpy(f+10,ap.bssid,6); memcpy(f+16,ap.bssid,6);
    f[34]=1; f[36]=42; f[37]=1; f[38]=2; f[39]=1; f[40]=1; f[41]=0x82; f[42]=0; f[43]=0; put32(f+44,3);
    post(&m,0x1b,0x8000,beacon,76);
    assert(!il3945_station_poll(&native,now+2) && native.last_beacon==now+2);
    assert(native.commands.command==0x11 && !native.bss_dirty);
    native.last_rx=native.last_ack=native.last_beacon=native.thermal_ms=0xfffffff0u;
    assert(!il3945_station_poll(&native,16) && !native.probing);
    /* Protected payloads failing authentication must not refresh liveness. */
    station_setup(&m,&e,&ap); m.secure=1; ap.security=IL_SECURITY_WPA2_PSK; ap.capability|=16;
    memcpy(ap.rsn,ap_rsn,sizeof(ap_rsn)); ap.rsn_length=sizeof(ap_rsn);
    /* Some APs omit beacon capability bits in the response; Linux does not
     * reject these. Reserved AID bits must not change the decoded station ID. */
    m.early_m1=1;
    m.override_capability=1; m.response_capability=0; m.response_aid=0xf801;
    assert(!il3945_station_connect(&native,&e,&ap,(const uint8_t*)"password",8,fixed_nonce,0));
    assert(native.aid==1 && native.active && native.security.authorized);
    assert(native.association_capability==0 && native.association_aid==0xf801);
    assert(m.eapol==2 && m.radio_associated);
    assert(native.tx_history_count==4);
    assert(native.tx_history[2].ether_type==0x888e && native.tx_history[2].completed);
    assert(native.tx_history[3].ether_type==0x888e && native.tx_history[3].completed);
    valid_at=native.last_rx;
    uint8_t invalid[100]={0}; invalid[8]=1; invalid[10]=1; invalid[14]=48;
    f=invalid+16; f[0]=8; f[1]=0x42;
    memcpy(f+4,e.mac,6); memcpy(f+10,ap.bssid,6); memcpy(f+16,ap.bssid,6);
    put32(f+48,3); post(&m,0x1b,0x8000,invalid,80);
    assert(!il3945_station_poll(&native,valid_at+1) && native.last_rx==valid_at);
    m.t.m.regs[CSR_INT/4]=CSR_INT_BIT_SW_ERR;
    assert(il3945_station_poll(&native,valid_at+2)==IL_VERIFY);
    assert(native.failure_security_phase==2 && native.failure_authorized && native.failure_active);
    assert(!native.security.authorized && !native.active);
    puts("iwlegacy protocol: association IEs, duplicate retries, live ERP, probes and validated liveness passed");
}

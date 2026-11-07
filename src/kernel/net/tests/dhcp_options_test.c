/* Router model only returns DNS/gateway when requested in DHCP option 55. */
#include "../dhcp.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct udp_socket { unsigned used; } peer;
static udp_data_handler_t handler;
static unsigned sends;
static uint32_t configured, configured_gateway;
mutex_t kernel_mutex;
void mutex_lock(mutex_t* m) { (void)m; }
void mutex_unlock(mutex_t* m) { (void)m; }
int scheduler_running(void) { return 1; }
uint64_t scheduler_clock(void) { static uint64_t clock; return ++clock*1000000; }
void task_sleep_ms(uint32_t n) { (void)n; }
void sys_net_poll(void) { }
uint8_t sys_net_is_ready(void) { return 1; }
uint64_t sys_net_get_mac_address(void) { return 0x020000000001ull; }
void rtc_get_time(rtc_time_t* t) { t->second=0; }
void ipv4_set_configuration(uint32_t ip, uint32_t gateway, uint32_t mask) {
    (void)mask; configured=ip; configured_gateway=gateway;
}
udp_socket_t* udp_connect(uint32_t ip,uint16_t port) { assert(ip==0xffffffffu && port==67); peer.used=1; return &peer; }
void udp_set_local_port(udp_socket_t* s,uint16_t p) { assert(s==&peer && p==68); }
void udp_bind(udp_socket_t* s,udp_data_handler_t h) { assert(s==&peer); handler=h; }
void udp_disconnect(udp_socket_t* s) { assert(s==&peer); peer.used=0; }
uint8_t udp_send(udp_socket_t* s,const uint8_t* data,uint16_t length) {
    assert(s==&peer && peer.used);
    unsigned requested=0, message=0;
    for(unsigned i=240;i<length;) {
        unsigned code=data[i++]; if(code==255) break; if(!code) continue;
        unsigned n=data[i++]; assert(i+n<=length);
        if(code==53) message=data[i];
        if(code==55) for(unsigned j=0;j<n;j++) {
            if(data[i+j]==1) requested|=1;
            if(data[i+j]==3) requested|=2;
            if(data[i+j]==6) requested|=4;
        }
        i+=n;
    }
    assert(requested==7 && (message==1 || message==3)); sends++;
    uint8_t reply[300]={2,1,6};
    memcpy(reply+4,data+4,4); memcpy(reply+28,data+28,6);
    write_be32(reply+16,0xc0a80164); write_be32(reply+236,DHCP_MAGIC);
    unsigned i=240;
    reply[i++]=53; reply[i++]=1; reply[i++]=message==1?2:5;
    reply[i++]=54; reply[i++]=4; write_be32(reply+i,0xc0a80101); i+=4;
    reply[i++]=1; reply[i++]=4; write_be32(reply+i,0xffffff00); i+=4;
    reply[i++]=3; reply[i++]=4; write_be32(reply+i,0xc0a80101); i+=4;
    reply[i++]=6; reply[i++]=4; write_be32(reply+i,0xc0a80101); i+=4;
    reply[i++]=255;
    handler(s,0xc0a80101,67,reply,i); return 1;
}
int main(void) {
    dhcp_lease_t lease;
    assert(dhcp_request(&lease,1));
    assert(sends==2 && lease.leased && lease.dns==0xc0a80101);
    assert(configured==0xc0a80164 && configured_gateway==0xc0a80101);
    puts("DHCP: DNS/gateway requested in DISCOVER and REQUEST, lease applied passed");
}

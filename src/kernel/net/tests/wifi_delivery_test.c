/* Actual IPv4/UDP/DNS stack, with a single-slot Wi-Fi-like Ethernet peer. */
#include "../ipv4.h"
#include "../udp.h"
#include "../dns.h"
#include "../dhcp.h"
#include "../etherframe.h"
#include "../../mutex.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
mutex_t kernel_mutex;
void mutex_lock(mutex_t* m) { (void)m; }
void mutex_unlock(mutex_t* m) { (void)m; }
volatile uint32_t frame_timer;
static dhcp_lease_t lease = {.leased=1,.dns=0xc0a80101};
const dhcp_lease_t* dhcp_get_lease(void) { return &lease; }
void task_sleep_ms(uint32_t ms) { frame_timer += ms; }
void debug_print(const char* s) { (void)s; }
void debug_hex(uint32_t n) { (void)n; }
uint32_t paging_current_directory(void) { return 0x1000; }
uint32_t paging_kernel_directory(void) { return 0x1000; }
uint32_t app_loader_context_enter(uint32_t n) { return n; }
void app_loader_context_leave(uint32_t n) { (void)n; }
void sys_net_set_ip_address(uint32_t n) { (void)n; }
void arp_set_local_ip(uint32_t n) { (void)n; }
void etherframe_init(const uint8_t mac[6]) { (void)mac; }
void etherframe_set_handler(uint16_t type, etherframe_handler_t h) { (void)type; (void)h; }
uint64_t etherframe_mac_from_bytes(const uint8_t mac[6]) {
    uint64_t n=0; for(unsigned i=0;i<6;i++) n=(n<<8)|mac[i]; return n;
}
static unsigned busy, sent, resolved=1;
static uint8_t packet[1500]; static unsigned packet_length;
uint8_t etherframe_send(uint64_t mac, uint16_t type, const uint8_t* p, uint32_t n) {
    (void)mac; assert(type==0x800 && n<=sizeof(packet));
    if(busy) return 0;
    memcpy(packet,p,n); packet_length=n; sent++; busy=1; return 1;
}
static uint8_t resolve(uint32_t ip, uint8_t mac[6]) {
    (void)ip; memset(mac,2,6); return resolved;
}
static void dns_reply(uint32_t ip) {
    /* Answer the real UDP query, with a compressed answer name and checksum. */
    uint8_t response[1500]; memcpy(response,packet,packet_length);
    unsigned query=packet_length-28, length=packet_length+16;
    response[2]=length>>8; response[3]=length;
    memcpy(response+16,packet+12,4); memcpy(response+12,packet+16,4);
    response[20]=0; response[21]=53; response[22]=packet[20]; response[23]=packet[21];
    response[24]=(query+24)>>8; response[25]=query+24;
    response[26]=response[27]=0; /* zero UDP checksum is valid for IPv4 */
    response[30]=0x81; response[31]=0x80; response[34]=0; response[35]=1;
    uint8_t* a=response+packet_length;
    const uint8_t rr[]={0xc0,12,0,1,0,1,0,0,0,60,0,4};
    memcpy(a,rr,sizeof(rr));
    a[12]=ip>>24; a[13]=ip>>16; a[14]=ip>>8; a[15]=ip;
    response[10]=response[11]=0;
    uint16_t sum=ipv4_checksum(response,20); response[10]=sum>>8; response[11]=sum;
    ipv4_receive_frame(response,length);
}
int main(void) {
    uint8_t mac[6]={2,0,0,0,0,1};
    ipv4_init(0xc0a80164,mac,0xc0a80101,0xffffff00); ipv4_set_mac_resolver(resolve); udp_init();
    /* DHCP/ARP or another client still owns the NIC's only transmit slot. */
    busy=1; assert(dns_resolve_async_start("example.com")); assert(!sent);
    uint32_t address=0;
    assert(!dns_resolve_async_poll(&address));
    busy=0; ipv4_poll(); assert(sent==1 && packet[23]==53);
    dns_reply(0x5db8d822); assert(dns_resolve_async_poll(&address)==1 && address==0x5db8d822);
    /* Lost first UDP packet: retry keeps its transaction ID and local port. */
    busy=0; assert(dns_resolve_async_start("example.org"));
    uint8_t id[2]={packet[28],packet[29]}, port[2]={packet[20],packet[21]};
    busy=0; frame_timer+=100; assert(!dns_resolve_async_poll(&address));
    assert(packet[28]==id[0] && packet[29]==id[1] && packet[20]==port[0] && packet[21]==port[1]);
    dns_reply(0x01020304); assert(dns_resolve_async_poll(&address)==1 && address==0x01020304);
    /* Repeated loss is bounded, then the existing fallback server is used. */
    busy=0; assert(dns_resolve_async_start("example.net"));
    unsigned initial=sent, start=frame_timer;
    for(unsigned step=1;step<=4;step++) {
        busy=0; frame_timer=start+step*100;
        assert(!dns_resolve_async_poll(&address));
        assert(sent==initial+(step<2?step:2));
    }
    busy=0; frame_timer=start+500;
    assert(!dns_resolve_async_poll(&address));
    assert(packet[16]==1 && packet[17]==1 && packet[18]==1 && packet[19]==1);
    dns_reply(0x05060708); assert(dns_resolve_async_poll(&address)==1 && address==0x05060708);
    /* Queue owns a copy, bounds backlog, and expires it rather than blocking. */
    busy=1; uint8_t data[8]={1,2,3,4};
    for(unsigned i=0;i<16;i++) assert(ipv4_send(0xc0a80101,1,data,sizeof(data)));
    assert(!ipv4_send(0xc0a80101,1,data,sizeof(data)));
    memset(data,0,sizeof(data)); busy=0; ipv4_poll(); assert(packet[20]==1 && packet[21]==2);
    unsigned before=sent; frame_timer+=200; busy=0; ipv4_poll(); assert(sent==before);
    /* ARP miss follows the same delivery path. */
    resolved=0; assert(ipv4_send(0xc0a80101,1,data,sizeof(data))); ipv4_poll(); assert(sent==before);
    resolved=1; ipv4_poll(); assert(sent==before+1);
    puts("Wi-Fi delivery: busy NIC DNS, real UDP/IP reply, loss retry, queue bounds/copy/expiry and ARP passed");
}

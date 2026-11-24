/* Real TCP state machine; deterministic packet loss, clock and network peer. */
#include "../tcp.c"
extern int puts(const char*);
extern void exit(int) __attribute__((noreturn));
#define CHECK(c) do { if (!(c)) { puts("FAIL: " #c); exit(1); } } while (0)
mutex_t kernel_mutex;
void mutex_lock(mutex_t* m) { (void)m; }
void mutex_unlock(mutex_t* m) { (void)m; }
volatile uint32_t frame_timer;
static ipv4_protocol_handler_t receiver;
static unsigned sent, accepted, received_count;
static int blocked, network_full, reject_accept;
static tcp_socket_t* accepted_socket;
static uint8_t received[32768];
static struct { unsigned length; uint8_t data[IPV4_MAX_PAYLOAD]; } packets[1024];
uint32_t ipv4_get_local_ip(void) { return 0x0a00020f; }
void ipv4_set_protocol_handler(uint8_t protocol, ipv4_protocol_handler_t handler) {
    CHECK(protocol==TCP_PROTOCOL); receiver=handler;
}
uint16_t ipv4_checksum(const void* data, uint32_t length) {
    const uint8_t* bytes=data; uint32_t sum=0;
    for (unsigned i=0;i<length;i+=2) sum+=(uint32_t)bytes[i]<<8 | (i+1<length ? bytes[i+1] : 0);
    while (sum>>16) sum=(sum&65535)+(sum>>16);
    return (uint16_t)~sum;
}
uint8_t ipv4_send(uint32_t destination, uint8_t protocol, const uint8_t* data, uint32_t length) {
    CHECK(protocol==TCP_PROTOCOL && length<=IPV4_MAX_PAYLOAD && sent<1024);
    CHECK(tcp_checksum(ipv4_get_local_ip(),destination,data,(uint16_t)length)==0);
    packets[sent].length=length;
    for (unsigned i=0;i<length;i++) packets[sent].data[i]=data[i];
    sent++; return !network_full;
}
static uint8_t receive_data(tcp_socket_t* s, const uint8_t* data, uint16_t length) {
    (void)s;
    if (blocked) return 0;
    CHECK(received_count+length<=sizeof(received));
    for (unsigned i=0;i<length;i++) received[received_count++]=data[i];
    return 1;
}
void syscall_tcp_on_accept(tcp_socket_t* listener, tcp_socket_t* child) {
    CHECK(listener->state==TCP_LISTEN && child->state==TCP_ESTABLISHED);
    accepted++; accepted_socket=child;
    if (reject_accept) tcp_release(child);
    else tcp_bind(child,receive_data);
}
static unsigned payload_size(unsigned i) { return packets[i].length-(packets[i].data[12]>>4)*4u; }
static uint32_t sequence_at(unsigned i) { return read_be32(packets[i].data+4); }
static uint16_t window_at(unsigned i) { return read_be16(packets[i].data+14); }
static void reset(void) {
    uint8_t* p=(uint8_t*)sockets;
    for (unsigned i=0;i<sizeof(sockets);i++) p[i]=0;
    frame_timer=0; sent=accepted=received_count=0; blocked=network_full=reject_accept=0;
    accepted_socket=0; next_ephemeral_port=49152; initial_sequence=0xc001cafe;
    tcp_init();
}
static void inject(tcp_socket_t* s, uint32_t seq, uint32_t ack, uint8_t flags,
                     uint16_t window, const void* data, unsigned length, int mss) {
    uint8_t packet[IPV4_MAX_PAYLOAD];
    unsigned header=mss ? 24u : 20u;
    CHECK(header+length<=sizeof(packet));
    for (unsigned i=0;i<header;i++) packet[i]=0;
    write_be16(packet,s->remote_port); write_be16(packet+2,s->local_port);
    write_be32(packet+4,seq); write_be32(packet+8,ack);
    packet[12]=(uint8_t)((header/4u)<<4); packet[13]=flags;
    write_be16(packet+14,window);
    if(mss) { packet[20]=2; packet[21]=4; write_be16(packet+22,(uint16_t)mss); }
    for(unsigned i=0;i<length;i++) packet[header+i]=((const uint8_t*)data)[i];
    write_be16(packet+16,tcp_checksum(s->remote_ip,s->local_ip,packet,(uint16_t)(header+length)));
    receiver(s->remote_ip,s->local_ip,packet,header+length);
}
static tcp_socket_t* connected(unsigned window, int mss) {
    tcp_socket_t* s=tcp_connect(0x0a000202,80);
    CHECK(s && s->state==TCP_SYN_SENT && (packets[sent-1].data[13]&TCP_FLAG_SYN));
    CHECK(packets[sent-1].length==24 && read_be16(packets[sent-1].data+22)==TCP_MAX_PAYLOAD);
    inject(s,1000,s->send_next,TCP_FLAG_SYN|TCP_FLAG_ACK,(uint16_t)window,0,0,mss);
    CHECK(s->state==TCP_ESTABLISHED && s->receive_next==1001);
    tcp_bind(s,receive_data); sent=0; return s;
}
static void ack_current(tcp_socket_t* s, unsigned window) {
    inject(s,s->receive_next,s->send_next,TCP_FLAG_ACK,(uint16_t)window,0,0,0);
}
static void test_handshake(void) {
    reset(); network_full=1;
    tcp_socket_t* s=tcp_connect(0x0a000202,80); CHECK(s);
    uint32_t seq=s->send_unacknowledged;
    network_full=0; frame_timer=100; tcp_poll();
    CHECK(sent==2 && sequence_at(0)==seq && sequence_at(1)==seq && s->rto==200);
    inject(s,1000,s->send_next,TCP_FLAG_SYN|TCP_FLAG_ACK,4096,0,0,1000);
    CHECK(s->state==TCP_ESTABLISHED && s->peer_mss==1000 && !s->outstanding);
    /* Lost final handshake ACK: duplicate SYN-ACK elicits another ACK. */
    unsigned n=sent;
    inject(s,1000,s->send_next,TCP_FLAG_SYN|TCP_FLAG_ACK,4096,0,0,1000);
    CHECK(sent==n+1 && packets[sent-1].data[13]==TCP_FLAG_ACK);
    reset(); s=tcp_connect(0x0a000202,80); frame_timer=TCP_HANDSHAKE_TIMEOUT; tcp_poll();
    CHECK(s->state==TCP_CLOSED && s->in_use);
    CHECK(tcp_connect(0x0a000202,80)!=s); tcp_release(s); CHECK(!s->in_use);
    reset(); frame_timer=0xfffffff0u; s=tcp_connect(0x0a000202,80);
    frame_timer+=100; tcp_poll(); CHECK(s->retries==1);
    reset(); s=tcp_connect(0x0a000202,80);
    inject(s,500,0,TCP_FLAG_SYN,4096,0,0,0); CHECK(s->state==TCP_SYN_RECEIVED);
    inject(s,500,s->send_next,TCP_FLAG_SYN|TCP_FLAG_ACK,4096,0,0,0);
    CHECK(s->state==TCP_ESTABLISHED);
}
static void test_send(void) {
    reset(); tcp_socket_t* s=connected(4096,256);
    static uint8_t data[4000]; for(unsigned i=0;i<sizeof(data);i++) data[i]=(uint8_t)i;
    uint32_t start=s->send_next;
    CHECK(tcp_send(s,data,1448) && tcp_send(s,data+1448,1448) && tcp_send(s,data+2896,1104));
    CHECK(sent==1 && payload_size(0)==256);
    frame_timer=100; tcp_poll(); CHECK(sent==2 && sequence_at(1)==start && payload_size(1)==256);
    inject(s,s->receive_next,start+100,TCP_FLAG_ACK,64,0,0,0);
    CHECK(s->out_length==156 && s->send_unacknowledged==start+100);
    frame_timer+=s->rto; tcp_poll(); CHECK(payload_size(sent-1)==64 && sequence_at(sent-1)==start+100);
    CHECK(packets[sent-1].data[20]==data[100]);
    while(s->send_unacknowledged!=s->send_end) {
        unsigned last=sent-1; CHECK(payload_size(last)<=256);
        for(unsigned i=0;i<payload_size(last);i++)
            CHECK(packets[last].data[20+i]==data[(sequence_at(last)-start)+i]);
        ack_current(s,4096);
    }
    CHECK(!s->outstanding && s->send_end-start==4000);
    reset(); s=connected(4096,1448);
    for(unsigned i=0;i<5;i++) CHECK(tcp_send(s,data,1448));
    CHECK(!tcp_send(s,data,1448)); /* bounded queue backpressure */
    CHECK(s->send_end-s->send_unacknowledged==7240);
    ack_current(s,4096); CHECK(tcp_send(s,data,1448));
    reset(); s=connected(4096,1448);
    s->send_next=s->send_end=s->send_unacknowledged=0xfffffff8u;
    s->window_ack=s->send_next;
    CHECK(tcp_send(s,data,32)); CHECK(s->send_next==24);
    inject(s,s->receive_next,25,TCP_FLAG_ACK,4096,0,0,0);
    CHECK(s->state==TCP_ESTABLISHED && s->send_unacknowledged==0xfffffff8u);
    ack_current(s,4096); CHECK(s->send_unacknowledged==24 && !s->outstanding);
}
static void test_receive(void) {
    reset(); tcp_socket_t* s=connected(4096,1448); uint32_t base=s->receive_next;
    inject(s,base+6,s->send_next,TCP_FLAG_ACK,4096,"world",5,0);
    CHECK(received_count==0 && read_be32(packets[sent-1].data+8)==base);
    inject(s,base,s->send_next,TCP_FLAG_ACK,4096,"hello ",6,0);
    CHECK(received_count==11);
    const char* expected="hello world";
    for(unsigned i=0;i<11;i++) CHECK(received[i]==(uint8_t)expected[i]);
    inject(s,base,s->send_next,TCP_FLAG_ACK,4096,"hello world",11,0);
    CHECK(received_count==11);
    inject(s,base+8,s->send_next,TCP_FLAG_ACK,4096,"rld!!!",6,0);
    CHECK(received_count==14 && received[11]=='!' && received[13]=='!');
    inject(s,s->receive_next,s->send_next,TCP_FLAG_ACK|TCP_FLAG_FIN,4096,"end",3,0);
    CHECK(received_count==17 && received[14]=='e' && received[16]=='d' && s->state==TCP_CLOSE_WAIT);
    CHECK(tcp_send(s,(const uint8_t*)"reply",5)); /* half-close permits replies */
    reset(); s=connected(4096,1448); base=s->receive_next;
    inject(s,base+5,s->send_next,TCP_FLAG_ACK|TCP_FLAG_FIN,4096,0,0,0);
    CHECK(s->state==TCP_ESTABLISHED && s->fin_pending);
    inject(s,base,s->send_next,TCP_FLAG_ACK,4096,"hello",5,0);
    CHECK(received_count==5 && s->state==TCP_CLOSE_WAIT && s->receive_next==base+6);
    reset(); s=connected(4096,1448); s->receive_next=s->receive_read=0xfffffffcu;
    inject(s,0,s->send_next,TCP_FLAG_ACK,4096,"EF",2,0);
    inject(s,0xfffffffcu,s->send_next,TCP_FLAG_ACK,4096,"ABCD",4,0);
    CHECK(received_count==6 && s->receive_next==2 && received[5]=='F');
}
static void test_stream(void) {
    reset(); tcp_socket_t* s=connected(1700,512);
    static uint8_t data[24000];
    for(unsigned i=0;i<sizeof(data);i++) data[i]=(uint8_t)(i*73u+i/256u);
    uint32_t start=s->send_next;
    unsigned queued=0, delivered=0, segments=0;
    while (delivered<sizeof(data)) {
        while (queued<sizeof(data)) {
            unsigned n=minimum(1448,sizeof(data)-queued);
            if (!tcp_send(s,data+queued,(uint16_t)n)) break;
            queued+=n;
        }
        CHECK(s->outstanding && payload_size(sent-1)>0);
        unsigned packet=sent-1, count=payload_size(packet);
        CHECK(sequence_at(packet)==start+delivered && count<=512);
        for(unsigned i=0;i<count;i++) CHECK(packets[packet].data[20+i]==data[delivered+i]);
        if (segments++%5==0) {
            frame_timer+=s->rto; tcp_poll();
            CHECK(sequence_at(sent-1)==sequence_at(packet) && payload_size(sent-1)==count);
            for(unsigned i=0;i<count;i++) CHECK(packets[sent-1].data[20+i]==data[delivered+i]);
        }
        delivered+=count; ack_current(s,1700);
    }
    CHECK(queued==sizeof(data) && !s->outstanding && s->send_unacknowledged==s->send_end);
}
static void test_windows(void) {
    reset(); tcp_socket_t* s=connected(4096,1448); uint32_t base=s->receive_next;
    blocked=1; static uint8_t data[1448];
    for(unsigned i=0;i<4096;) {
        unsigned n=minimum(1448,4096-i);
        inject(s,base+i,s->send_next,TCP_FLAG_ACK,4096,data,n,0); i+=n;
    }
    CHECK(received_count==0 && receive_window(s)==0 && window_at(sent-1)==0);
    inject(s,s->receive_next,s->send_next,TCP_FLAG_ACK,4096,"x",1,0);
    CHECK(s->receive_next==base+4096 && receive_window(s)==0);
    blocked=0; tcp_poll(); CHECK(received_count==4096 && window_at(sent-1)==4096);
    unsigned n=received_count;
    inject(s,base,s->send_next,TCP_FLAG_ACK,4096,data,1448,0); CHECK(received_count==n);
    reset(); s=connected(0,1448);
    CHECK(tcp_send(s,(const uint8_t*)"pending",7) && !s->outstanding && sent==0);
    frame_timer=100; tcp_poll(); CHECK(sent==1 && sequence_at(0)==s->send_next-1 && payload_size(0)==0);
    ack_current(s,3); CHECK(s->outstanding && payload_size(sent-1)==3);
    ack_current(s,3); CHECK(payload_size(sent-1)==3);
    ack_current(s,3); CHECK(payload_size(sent-1)==1);
    ack_current(s,0); CHECK(!s->outstanding);
    CHECK(tcp_send(s,(const uint8_t*)"alive",5));
    for(unsigned i=0;i<200;i++) { frame_timer+=100; ack_current(s,0); tcp_poll(); }
    CHECK(s->state==TCP_ESTABLISHED); /* responsive zero-window peer is retained */
    reset(); s=connected(4096,1448);
    CHECK(tcp_send(s,(const uint8_t*)"inflight",8));
    inject(s,s->receive_next,s->send_unacknowledged,TCP_FLAG_ACK,0,0,0,0);
    for(unsigned i=0;i<12;i++) {
        frame_timer+=s->rto;
        inject(s,s->receive_next,s->send_unacknowledged,TCP_FLAG_ACK,0,0,0,0);
        tcp_poll();
        CHECK(payload_size(sent-1)==1 && s->state==TCP_ESTABLISHED);
    }
    CHECK(s->retries==0); /* persist probes do not exhaust the loss budget */
    reset(); s=connected(0,1448); CHECK(tcp_send(s,(const uint8_t*)"dead",4));
    frame_timer=TCP_STALL_TIMEOUT; tcp_poll(); CHECK(s->state==TCP_CLOSED);
}
static void test_close(void) {
    reset(); tcp_socket_t* s=connected(4096,1448); static uint8_t data[1448];
    CHECK(tcp_send(s,data,1448) && tcp_send(s,data,1448));
    uint32_t end=s->send_end;
    tcp_release(s); CHECK(s->in_use && s->detached && s->state==TCP_ESTABLISHED);
    ack_current(s,4096); CHECK(!(packets[sent-1].data[13]&TCP_FLAG_FIN));
    ack_current(s,4096); CHECK(s->state==TCP_FIN_WAIT1 && (packets[sent-1].data[13]&TCP_FLAG_FIN));
    CHECK(sequence_at(sent-1)==end);
    frame_timer+=s->rto; tcp_poll(); CHECK(sequence_at(sent-1)==end && (packets[sent-1].data[13]&TCP_FLAG_FIN));
    ack_current(s,4096); CHECK(s->state==TCP_FIN_WAIT2);
    inject(s,s->receive_next,s->send_next,TCP_FLAG_ACK|TCP_FLAG_FIN,4096,0,0,0);
    CHECK(s->state==TCP_TIME_WAIT && s->in_use);
    inject(s,s->receive_next,0,TCP_FLAG_RST,4096,0,0,0);
    CHECK(s->state==TCP_TIME_WAIT && s->in_use);
    uint32_t fin=s->receive_next-1;
    frame_timer+=100; inject(s,fin,s->send_next,TCP_FLAG_ACK|TCP_FLAG_FIN,4096,0,0,0);
    CHECK(s->state_started==frame_timer);
    frame_timer+=TCP_TIME_WAIT_TIMEOUT; tcp_poll(); CHECK(!s->in_use);
    reset(); s=connected(4096,1448); tcp_release(s);
    inject(s,s->receive_next,s->send_next-1,TCP_FLAG_ACK|TCP_FLAG_FIN,4096,0,0,0);
    CHECK(s->state==TCP_CLOSING); ack_current(s,4096); CHECK(s->state==TCP_TIME_WAIT);
    reset(); s=connected(4096,1448);
    inject(s,s->receive_next,s->send_next,TCP_FLAG_ACK|TCP_FLAG_FIN,4096,0,0,0);
    tcp_release(s); CHECK(s->state==TCP_LAST_ACK); ack_current(s,4096); CHECK(!s->in_use);
    reset(); s=connected(4096,1448); tcp_release(s); ack_current(s,4096);
    frame_timer+=TCP_CLOSE_TIMEOUT; tcp_poll(); CHECK(!s->in_use);
}
static void test_listener(void) {
    reset(); tcp_socket_t* listener=tcp_listen(8080); CHECK(listener && !tcp_listen(8080));
    tcp_socket_t peer={0}; peer.local_ip=listener->local_ip; peer.local_port=8080;
    peer.remote_ip=0x0a000202; peer.remote_port=12345;
    inject(&peer,2000,0,TCP_FLAG_RST,4096,0,0,0); CHECK(listener->state==TCP_LISTEN);
    inject(&peer,2000,0,TCP_FLAG_SYN,4096,0,0,512);
    CHECK(accepted==0 && sent==1);
    uint32_t syn=sequence_at(0);
    inject(&peer,2000,0,TCP_FLAG_SYN,4096,0,0,512);
    CHECK(accepted==0 && sequence_at(sent-1)==syn);
    inject(&peer,2001,syn+2,TCP_FLAG_ACK,4096,0,0,0); CHECK(accepted==0);
    inject(&peer,2001,syn+1,TCP_FLAG_ACK,4096,"GET",3,0);
    CHECK(accepted==1 && received_count==3 && accepted_socket->peer_mss==512);
    tcp_release(listener); CHECK(!listener->in_use && accepted_socket->in_use);
    reset(); listener=tcp_listen(8080); peer.local_port=8080;
    inject(&peer,2000,0,TCP_FLAG_SYN,4096,0,0,0);
    frame_timer=TCP_HANDSHAKE_TIMEOUT; tcp_poll();
    unsigned live=0; for(unsigned i=0;i<TCP_MAX_SOCKETS;i++) live+=sockets[i].in_use;
    CHECK(live==1 && accepted==0);
    reset(); listener=tcp_listen(8080); reject_accept=1;
    inject(&peer,2000,0,TCP_FLAG_SYN,4096,0,0,0); syn=sequence_at(0);
    inject(&peer,2001,syn+1,TCP_FLAG_ACK,4096,0,0,0);
    CHECK(accepted==1 && accepted_socket->detached && accepted_socket->state==TCP_FIN_WAIT1);
}
static void test_validation(void) {
    reset();
    CHECK(!tcp_connect(0xffffffffu,80) && !tcp_connect(0xe0000001u,80));
    tcp_socket_t unknown={0}; unknown.local_ip=ipv4_get_local_ip(); unknown.remote_ip=0x0a000202;
    unknown.local_port=9000; unknown.remote_port=12345;
    inject(&unknown,100,200,TCP_FLAG_ACK,4096,"abc",3,0);
    CHECK(sent==1 && sequence_at(0)==200 && packets[0].data[13]==TCP_FLAG_RST);
    inject(&unknown,100,0,TCP_FLAG_SYN,4096,"abc",3,0);
    CHECK(sent==2 && read_be32(packets[1].data+8)==104 && packets[1].data[13]==(TCP_FLAG_RST|TCP_FLAG_ACK));
    inject(&unknown,100,200,TCP_FLAG_RST,4096,0,0,0); CHECK(sent==2);
    reset(); tcp_socket_t* s=connected(4096,1448);
    inject(s,s->receive_next+5000,0,TCP_FLAG_RST,4096,0,0,0); CHECK(s->state==TCP_ESTABLISHED);
    inject(s,s->receive_next+1,0,TCP_FLAG_RST,4096,0,0,0); CHECK(s->state==TCP_ESTABLISHED);
    inject(s,s->receive_next,s->send_next+1,TCP_FLAG_ACK,4096,"bad",3,0);
    CHECK(s->state==TCP_ESTABLISHED && received_count==0);
    inject(s,s->receive_next,0,TCP_FLAG_RST,4096,0,0,0); CHECK(s->state==TCP_CLOSED && s->in_use);
    reset(); s=tcp_connect(0x0a000202,80);
    inject(s,0,s->send_next+1,TCP_FLAG_RST|TCP_FLAG_ACK,4096,0,0,0); CHECK(s->state==TCP_SYN_SENT);
    inject(s,0,s->send_next,TCP_FLAG_RST|TCP_FLAG_ACK,4096,0,0,0); CHECK(s->state==TCP_CLOSED);
    reset(); s=connected(4096,1448); unsigned n=sent;
    uint8_t bad[24]={0};
    write_be16(bad,s->remote_port); write_be16(bad+2,s->local_port);
    write_be32(bad+4,s->receive_next); write_be32(bad+8,s->send_next);
    bad[12]=6<<4; bad[13]=TCP_FLAG_ACK; bad[20]=2; bad[21]=0;
    write_be16(bad+16,tcp_checksum(s->remote_ip,s->local_ip,bad,24));
    receiver(s->remote_ip,s->local_ip,bad,24); CHECK(sent==n);
    bad[21]=4; bad[22]=2; bad[23]=0; /* checksum now corrupt */
    receiver(s->remote_ip,s->local_ip,bad,24); CHECK(sent==n);
    receiver(s->remote_ip,0xffffffffu,bad,24); CHECK(sent==n);
    receiver(0xe0000001u,s->local_ip,bad,24); CHECK(sent==n);
    reset(); s=connected(4096,1448); CHECK(tcp_send(s,(const uint8_t*)"lost",4));
    for(unsigned i=0;i<20 && s->state!=TCP_CLOSED;i++) { frame_timer+=s->rto; tcp_poll(); }
    CHECK(s->state==TCP_CLOSED && s->in_use);
    reset(); s=connected(4096,1448); uint32_t start=s->send_next;
    static uint8_t data[1024]; CHECK(tcp_send(s,data,sizeof(data)));
    inject(s,s->receive_next+10,start,TCP_FLAG_ACK,300,0,0,0);
    inject(s,s->receive_next,start+100,TCP_FLAG_ACK,65000,0,0,0);
    CHECK(s->window_ack==start && s->peer_window==300);
    frame_timer+=s->rto; tcp_poll();
    CHECK(sequence_at(sent-1)==start+100 && payload_size(sent-1)==200);
}
int main(void) {
    test_handshake(); test_send(); test_stream(); test_receive(); test_windows(); test_close(); test_listener(); test_validation();
    puts("PASS: TCP loss/retry/backoff, partial ACK/MSS, sequence wrap, reassembly/duplicates, zero windows, buffered close/FIN/TIME_WAIT, accept lifecycle and malformed/reset validation");
    return 0;
}

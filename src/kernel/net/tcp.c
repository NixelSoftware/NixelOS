#include "../mutex.h"
#include "tcp.h"
#include "ipv4.h"

#define TCP_HEADER_SIZE 20u
#define TCP_WINDOW_SIZE 4096u
#define TCP_TX_SIZE 8192u
#define TCP_RTO_MIN 100u        /* frame_timer: 100 Hz */
#define TCP_RTO_MAX 6000u
#define TCP_HANDSHAKE_TIMEOUT 3000u
#define TCP_STALL_TIMEOUT 18000u
#define TCP_CLOSE_TIMEOUT 6000u
#define TCP_TIME_WAIT_TIMEOUT 6000u /* two implementation MSLs of 30 seconds */
#define TCP_RETRIES 8u
extern volatile uint32_t frame_timer;
extern void syscall_tcp_on_accept(tcp_socket_t*, tcp_socket_t*);

struct tcp_socket {
    uint8_t in_use, detached, close_requested, local_fin, peer_fin;
    tcp_socket_state_t state;
    uint16_t local_port, remote_port, peer_mss, peer_window;
    uint32_t local_ip, remote_ip;
    uint32_t send_next, send_unacknowledged, send_end;
    uint32_t receive_next, receive_read, window_sequence, window_ack;
    uint32_t created, state_started, last_received;
    uint32_t rto, srtt, rttvar, probe_started, probe_interval;
    uint8_t outstanding, out_flags, retries, retransmitted, fin_pending;
    uint16_t out_length, advertised_window;
    uint32_t out_sequence, sent_at, fin_sequence;
    tcp_socket_t* parent;
    tcp_data_handler_t handler;
    uint8_t tx[TCP_TX_SIZE], rx[TCP_WINDOW_SIZE], present[TCP_WINDOW_SIZE/8u];
};

static tcp_socket_t sockets[TCP_MAX_SOCKETS];
static uint16_t next_ephemeral_port = 49152;
static uint32_t initial_sequence = 0xC001CAFE;
static uint8_t segment_buffer[IPV4_MAX_PAYLOAD];
static uint8_t checksum_buffer[12u + IPV4_MAX_PAYLOAD];

static int before(uint32_t a, uint32_t b) { return (int32_t)(a-b) < 0; }
static uint32_t minimum(uint32_t a, uint32_t b) { return a < b ? a : b; }
static int unicast_peer(uint32_t ip) {
    return ip && ip!=0xffffffffu && (ip&0xf0000000u)!=0xe0000000u;
}
static uint16_t read_be16(const uint8_t* p) { return (uint16_t)((uint16_t)p[0]<<8 | p[1]); }
static uint32_t read_be32(const uint8_t* p) { return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static void write_be16(uint8_t* p, uint16_t n) { p[0]=n>>8; p[1]=n; }
static void write_be32(uint8_t* p, uint32_t n) { p[0]=n>>24; p[1]=n>>16; p[2]=n>>8; p[3]=n; }
static void set_state(tcp_socket_t* s, tcp_socket_state_t state) { s->state=state; s->state_started=frame_timer; }
static uint32_t new_sequence(void) {
    /* Counter progression plus the current tick avoids immediate ISN reuse.
     * This is not a cryptographic ISN generator. */
    initial_sequence += 64001u + frame_timer * 17u;
    return initial_sequence;
}
static void close_socket(tcp_socket_t* s) {
    set_state(s,TCP_CLOSED);
    s->outstanding=0;
    s->handler=0;
    /* An owned CLOSED socket remains allocated until its handle is released.
     * Otherwise a stale handle could refer to a newly allocated connection. */
    if (s->detached || s->parent) s->in_use=0;
    s->parent=0;
}
static tcp_socket_t* allocate_socket(void) {
    for (unsigned i=0;i<TCP_MAX_SOCKETS;i++) if (!sockets[i].in_use) {
        tcp_socket_t* s=&sockets[i];
        uint8_t* p=(uint8_t*)s;
        for (unsigned n=0;n<sizeof(*s);n++) p[n]=0;
        s->in_use=1; s->peer_mss=536; s->peer_window=65535;
        s->rto=TCP_RTO_MIN; s->probe_interval=TCP_RTO_MIN;
        s->created=s->state_started=s->last_received=s->probe_started=frame_timer;
        return s;
    }
    return 0;
}
static tcp_socket_t* find_socket(uint32_t source, uint16_t sport, uint32_t destination, uint16_t dport) {
    tcp_socket_t* listener=0;
    for (unsigned i=0;i<TCP_MAX_SOCKETS;i++) {
        tcp_socket_t* s=&sockets[i];
        if (!s->in_use || s->state==TCP_CLOSED || s->local_port!=dport || s->local_ip!=destination) continue;
        if (s->state==TCP_LISTEN) listener=s;
        else if (s->remote_ip==source && s->remote_port==sport) return s;
    }
    return listener;
}
static uint16_t tcp_checksum(uint32_t source, uint32_t destination, const uint8_t* data, uint16_t length) {
    write_be32(checksum_buffer,source); write_be32(checksum_buffer+4,destination);
    checksum_buffer[8]=0; checksum_buffer[9]=TCP_PROTOCOL; write_be16(checksum_buffer+10,length);
    for (unsigned i=0;i<length;i++) checksum_buffer[12+i]=data[i];
    return ipv4_checksum(checksum_buffer,12u+length);
}
static uint16_t receive_window(tcp_socket_t* s) {
    if (s->peer_fin) return 0;
    return (uint16_t)(TCP_WINDOW_SIZE-(s->receive_next-s->receive_read));
}
static uint8_t emit(uint32_t local, uint32_t remote, uint16_t lport, uint16_t rport,
                    uint32_t sequence, uint32_t ack, uint16_t window, uint8_t flags,
                    const uint8_t* data, uint16_t length) {
    unsigned header=(flags&TCP_FLAG_SYN) ? 24u : TCP_HEADER_SIZE;
    if (header+length>IPV4_MAX_PAYLOAD) return 0;
    write_be16(segment_buffer,lport); write_be16(segment_buffer+2,rport);
    write_be32(segment_buffer+4,sequence); write_be32(segment_buffer+8,ack);
    segment_buffer[12]=(uint8_t)((header/4u)<<4); segment_buffer[13]=flags;
    write_be16(segment_buffer+14,window); write_be16(segment_buffer+16,0); write_be16(segment_buffer+18,0);
    if (flags&TCP_FLAG_SYN) { segment_buffer[20]=2; segment_buffer[21]=4; write_be16(segment_buffer+22,TCP_MAX_PAYLOAD); }
    for (unsigned i=0;i<length;i++) segment_buffer[header+i]=data[i];
    write_be16(segment_buffer+16,tcp_checksum(local,remote,segment_buffer,(uint16_t)(header+length)));
    return ipv4_send(remote,TCP_PROTOCOL,segment_buffer,header+length);
}
static uint8_t wire(tcp_socket_t* s, uint32_t sequence, const uint8_t* data, uint16_t length, uint8_t flags) {
    uint16_t window=receive_window(s);
    uint8_t ok=emit(s->local_ip,s->remote_ip,s->local_port,s->remote_port,sequence,s->receive_next,window,flags,data,length);
    if (ok) s->advertised_window=window;
    return ok;
}
static void acknowledge(tcp_socket_t* s) { (void)wire(s,s->send_next,0,0,TCP_FLAG_ACK); }
static uint32_t send_window(tcp_socket_t* s, uint32_t sequence) {
    uint32_t edge=s->window_ack+s->peer_window;
    return before(sequence,edge) ? edge-sequence : 0;
}
static void reset_reply(uint32_t local, uint32_t remote, uint16_t lport, uint16_t rport,
                         uint32_t sequence, uint32_t ack, unsigned length, uint8_t flags) {
    if (flags&TCP_FLAG_RST) return;
    if (flags&TCP_FLAG_ACK) (void)emit(local,remote,lport,rport,ack,0,0,TCP_FLAG_RST,0,0);
    else (void)emit(local,remote,lport,rport,0,sequence+length+!!(flags&TCP_FLAG_SYN)+!!(flags&TCP_FLAG_FIN),
                     0,TCP_FLAG_RST|TCP_FLAG_ACK,0,0);
}
static uint8_t resend(tcp_socket_t* s) {
    /* The retained segment may cross the send-ring boundary. */
    static uint8_t data[TCP_MAX_PAYLOAD];
    for (unsigned i=0;i<s->out_length;i++) data[i]=s->tx[(s->out_sequence+i)&(TCP_TX_SIZE-1u)];
    unsigned length=s->out_length;
    uint32_t available=send_window(s,s->out_sequence);
    if (length>available) length=available ? available : 1;
    return wire(s,s->out_sequence,data,(uint16_t)length,s->out_flags);
}
static void start_segment(tcp_socket_t* s, uint16_t length, uint8_t flags) {
    s->out_sequence=s->send_next; s->out_length=length; s->out_flags=flags;
    s->outstanding=1; s->retries=s->retransmitted=0; s->sent_at=frame_timer;
    /* Retain even when the NIC/ARP queue is temporarily full. */
    s->send_next += length+!!(flags&TCP_FLAG_SYN)+!!(flags&TCP_FLAG_FIN);
    (void)resend(s);
}
static void flush(tcp_socket_t* s) {
    if (s->outstanding || (s->state!=TCP_ESTABLISHED && s->state!=TCP_CLOSE_WAIT)) return;
    uint32_t queued=s->send_end-s->send_next;
    uint32_t available=send_window(s,s->send_next);
    if (queued && available) {
        unsigned amount=minimum(queued,minimum(available,s->peer_mss));
        start_segment(s,(uint16_t)amount,TCP_FLAG_ACK|TCP_FLAG_PSH);
    } else if (!queued && s->close_requested && available) {
        s->local_fin=1;
        set_state(s,s->peer_fin ? TCP_LAST_ACK : TCP_FIN_WAIT1);
        start_segment(s,0,TCP_FLAG_FIN|TCP_FLAG_ACK);
    }
}
static void update_rto(tcp_socket_t* s, uint32_t sample) {
    if (!sample) sample=1;
    if (!s->srtt) { s->srtt=sample; s->rttvar=(sample+1)/2; }
    else {
        uint32_t difference=s->srtt>sample ? s->srtt-sample : sample-s->srtt;
        s->rttvar=(3*s->rttvar+difference)/4;
        s->srtt=(7*s->srtt+sample)/8;
    }
    s->rto=minimum(TCP_RTO_MAX,s->srtt+(s->rttvar ? 4*s->rttvar : 1));
    if (s->rto<TCP_RTO_MIN) s->rto=TCP_RTO_MIN;
}
static void process_ack(tcp_socket_t* s, uint32_t ack) {
    if (!before(s->send_unacknowledged,ack) || before(s->send_next,ack)) return;
    if (s->outstanding && !s->retransmitted) update_rto(s,frame_timer-s->sent_at);
    s->send_unacknowledged=ack;
    if (ack==s->send_next) {
        s->outstanding=0;
        if (s->state==TCP_FIN_WAIT1) set_state(s,TCP_FIN_WAIT2);
        else if (s->state==TCP_CLOSING) set_state(s,TCP_TIME_WAIT);
        else if (s->state==TCP_LAST_ACK) close_socket(s);
    } else if (s->outstanding && s->out_length) {
        uint32_t consumed=ack-s->out_sequence;
        s->out_sequence=ack; s->out_length-=(uint16_t)consumed;
        s->sent_at=frame_timer;
        /* No RTT measurement from a later ACK of the trimmed segment. */
        s->retransmitted=1;
    }
}
static int present(tcp_socket_t* s, uint32_t sequence) {
    unsigned index=sequence&(TCP_WINDOW_SIZE-1u);
    return (s->present[index/8u]>>(index%8u))&1;
}
static void drain_receive(tcp_socket_t* s) {
    uint32_t end=s->receive_next-!!s->peer_fin;
    while (s->handler && s->receive_read!=end) {
        unsigned index=s->receive_read&(TCP_WINDOW_SIZE-1u);
        unsigned count=minimum(end-s->receive_read,TCP_WINDOW_SIZE-index);
        if (!s->handler(s,s->rx+index,(uint16_t)count)) break;
        /* A callback may close the socket. Never call its old handler again. */
        if (!s->in_use || s->state==TCP_CLOSED) return;
        for (unsigned i=0;i<count;i++) {
            unsigned n=(index+i)&(TCP_WINDOW_SIZE-1u);
            s->present[n/8u]&=(uint8_t)~(1u<<(n%8u));
        }
        s->receive_read+=count;
    }
    if (s->detached) {
        for (unsigned i=0;i<sizeof(s->present);i++) s->present[i]=0;
        s->receive_read=end;
    }
}
static void consume_fin(tcp_socket_t* s) {
    if (!s->fin_pending || s->peer_fin || s->receive_next!=s->fin_sequence) return;
    s->receive_next++; s->peer_fin=1; s->fin_pending=0;
    if (s->state==TCP_ESTABLISHED) set_state(s,TCP_CLOSE_WAIT);
    else if (s->state==TCP_FIN_WAIT1) set_state(s,TCP_CLOSING);
    else if (s->state==TCP_FIN_WAIT2) set_state(s,TCP_TIME_WAIT);
}
static int parse_options(const uint8_t* packet, unsigned header, uint16_t* mss) {
    for (unsigned i=20;i<header;) {
        unsigned kind=packet[i];
        if (!kind) break;
        if (kind==1) { i++; continue; }
        if (i+1>=header || packet[i+1]<2 || packet[i+1]>header-i) return 0;
        if (kind==2) {
            if (packet[i+1]!=4 || !read_be16(packet+i+2)) return 0;
            *mss=(uint16_t)minimum(read_be16(packet+i+2),TCP_MAX_PAYLOAD);
        }
        i+=packet[i+1];
    }
    return 1;
}
static uint8_t tcp_receive(uint32_t source, uint32_t destination, const uint8_t* packet, uint32_t length) {
    MUTEX_GUARD(kernel_mutex);
    if (destination!=ipv4_get_local_ip() || !unicast_peer(source)) return 0;
    if (!packet || length<20 || length>IPV4_MAX_PAYLOAD || tcp_checksum(source,destination,packet,(uint16_t)length)) return 0;
    unsigned header=(packet[12]>>4)*4u;
    if (header<20 || header>length) return 0;
    uint16_t mss=536;
    if (!parse_options(packet,header,&mss)) return 0;
    uint16_t sport=read_be16(packet), dport=read_be16(packet+2), window=read_be16(packet+14);
    uint32_t sequence=read_be32(packet+4), ack=read_be32(packet+8);
    uint8_t flags=packet[13]; unsigned payload=length-header;
    if ((flags&TCP_FLAG_SYN) && (flags&TCP_FLAG_FIN)) return 0;
    tcp_socket_t* s=find_socket(source,sport,destination,dport);
    if (!s) { reset_reply(destination,source,dport,sport,sequence,ack,payload,flags); return 0; }
    if (s->state==TCP_LISTEN) {
        if (flags&TCP_FLAG_RST) return 0;
        if (flags&TCP_FLAG_ACK) { reset_reply(destination,source,dport,sport,sequence,ack,payload,flags); return 0; }
        if (!(flags&TCP_FLAG_SYN)) return 0;
        tcp_socket_t* child=allocate_socket();
        if (!child) return 0;
        child->local_ip=destination; child->remote_ip=source;
        child->local_port=dport; child->remote_port=sport; child->parent=s;
        child->receive_read=child->receive_next=sequence+1;
        child->send_next=child->send_unacknowledged=new_sequence();
        child->send_end=child->send_next+1;
        child->peer_mss=mss; child->peer_window=window;
        child->window_sequence=sequence; child->window_ack=child->send_end;
        child->handler=s->handler;
        set_state(child,TCP_SYN_RECEIVED);
        start_segment(child,0,TCP_FLAG_SYN|TCP_FLAG_ACK);
        return 0;
    }
    if (s->state==TCP_SYN_SENT) {
        int valid_ack=(flags&TCP_FLAG_ACK) && ack==s->send_next;
        if (flags&TCP_FLAG_RST) { if (valid_ack) close_socket(s); return 0; }
        if ((flags&TCP_FLAG_ACK) && !valid_ack) {
            reset_reply(destination,source,dport,sport,sequence,ack,payload,flags); return 0;
        }
        if (!(flags&TCP_FLAG_SYN)) return 0;
        s->peer_mss=mss; s->peer_window=window; s->window_sequence=sequence; s->window_ack=ack;
        s->receive_read=s->receive_next=sequence+1; s->last_received=frame_timer;
        if (!valid_ack) {
            set_state(s,TCP_SYN_RECEIVED); s->out_flags=TCP_FLAG_SYN|TCP_FLAG_ACK;
            s->retransmitted=1;
            (void)resend(s); return 0; /* simultaneous open */
        }
        process_ack(s,ack); set_state(s,TCP_ESTABLISHED); acknowledge(s);
        /* SYN payload is not accepted; ACK leaves it for ordinary retransmit. */
        return 0;
    }
    if (s->state==TCP_SYN_RECEIVED && (flags&TCP_FLAG_SYN) && !(flags&TCP_FLAG_ACK) && sequence==s->receive_next-1) {
        s->retransmitted=1;
        (void)resend(s); return 0;
    }
    if (s->state==TCP_SYN_RECEIVED && !s->parent && (flags&(TCP_FLAG_SYN|TCP_FLAG_ACK))==(TCP_FLAG_SYN|TCP_FLAG_ACK) &&
        sequence==s->receive_next-1 && ack==s->send_next) {
        s->peer_window=window; s->window_sequence=sequence; s->window_ack=ack;
        process_ack(s,ack); set_state(s,TCP_ESTABLISHED); acknowledge(s);
        s->last_received=frame_timer;
        return 0;
    }
    uint16_t available=receive_window(s);
    unsigned segment_length=payload+!!(flags&TCP_FLAG_FIN)+!!(flags&TCP_FLAG_SYN);
    int acceptable=segment_length ? available &&
        (before(sequence,s->receive_next+available) && before(s->receive_next,sequence+segment_length)) :
        available ? (uint32_t)(sequence-s->receive_next)<available : sequence==s->receive_next;
    if (flags&TCP_FLAG_RST) {
        if (s->state==TCP_TIME_WAIT) return 0;
        if (sequence==s->receive_next) close_socket(s);
        else if (acceptable) acknowledge(s);
        return 0;
    }
    if (!acceptable) {
        /* A duplicate FIN restarts TIME_WAIT and must be acknowledged. */
        if (s->state==TCP_TIME_WAIT && (flags&TCP_FLAG_FIN) && sequence+payload+1==s->receive_next)
            s->state_started=frame_timer;
        acknowledge(s); return 0;
    }
    if (flags&TCP_FLAG_SYN) { acknowledge(s); return 0; }
    if (!(flags&TCP_FLAG_ACK)) return 0;
    if (before(s->send_next,ack)) { acknowledge(s); return 0; }
    if (s->state==TCP_SYN_RECEIVED) {
        if (ack!=s->send_next) return 0;
        process_ack(s,ack); set_state(s,TCP_ESTABLISHED);
        tcp_socket_t* parent=s->parent; s->parent=0;
        if (parent) syscall_tcp_on_accept(parent,s);
        if (!s->in_use || s->state==TCP_CLOSED) return 0;
    } else process_ack(s,ack);
    if (!s->in_use || s->state==TCP_CLOSED) return 0;
    s->last_received=frame_timer;
    if (!before(ack,s->send_unacknowledged) &&
        (before(s->window_sequence,sequence) ||
         (s->window_sequence==sequence && !before(ack,s->window_ack)))) {
        s->peer_window=window; s->window_sequence=sequence; s->window_ack=ack;
        if (window) { s->probe_interval=TCP_RTO_MIN; s->probe_started=frame_timer; }
    }
    if (!s->peer_fin) {
        for (unsigned i=0;i<payload;i++) {
            uint32_t n=sequence+i;
            if (before(n,s->receive_next) || (uint32_t)(n-s->receive_read)>=TCP_WINDOW_SIZE ||
                (s->fin_pending && !before(n,s->fin_sequence))) continue;
            unsigned index=n&(TCP_WINDOW_SIZE-1u);
            if (!present(s,n)) { s->rx[index]=packet[header+i]; s->present[index/8u]|=(uint8_t)(1u<<(index%8u)); }
        }
        uint32_t fin=sequence+payload;
        if ((flags&TCP_FLAG_FIN) && !before(fin,s->receive_next) &&
            (uint32_t)(fin-s->receive_read)<TCP_WINDOW_SIZE && !s->fin_pending) {
            s->fin_pending=1; s->fin_sequence=fin;
        }
        while (s->receive_next-s->receive_read<TCP_WINDOW_SIZE && present(s,s->receive_next) &&
               (!s->fin_pending || before(s->receive_next,s->fin_sequence))) s->receive_next++;
        consume_fin(s); drain_receive(s);
    }
    if (payload || (flags&TCP_FLAG_FIN)) acknowledge(s);
    flush(s);
    return 0;
}

void tcp_poll(void) {
    MUTEX_GUARD(kernel_mutex);
    uint32_t now=frame_timer;
    for (unsigned i=0;i<TCP_MAX_SOCKETS;i++) {
        tcp_socket_t* s=&sockets[i];
        if (!s->in_use || s->state==TCP_CLOSED || s->state==TCP_LISTEN) continue;
        if ((s->state==TCP_SYN_SENT || s->state==TCP_SYN_RECEIVED) && now-s->created>=TCP_HANDSHAKE_TIMEOUT) { close_socket(s); continue; }
        if (s->state==TCP_TIME_WAIT) {
            if (now-s->state_started>=TCP_TIME_WAIT_TIMEOUT) close_socket(s);
            continue;
        }
        if (s->state==TCP_FIN_WAIT2 && now-s->state_started>=TCP_CLOSE_TIMEOUT) { close_socket(s); continue; }
        drain_receive(s);
        if (!s->in_use || s->state==TCP_CLOSED) continue;
        if (s->state!=TCP_SYN_SENT && s->state!=TCP_SYN_RECEIVED &&
            s->advertised_window!=receive_window(s)) acknowledge(s);
        if (s->outstanding) {
            if (now-s->sent_at>=s->rto) {
                int persist=s->out_length && !send_window(s,s->out_sequence);
                if (!persist && s->retries++>=TCP_RETRIES) { close_socket(s); continue; }
                s->retransmitted=1; s->sent_at=now;
                s->rto=minimum(TCP_RTO_MAX,s->rto*2);
                (void)resend(s);
            }
        } else {
            flush(s);
            if (s->state==TCP_ESTABLISHED || s->state==TCP_CLOSE_WAIT) {
                if ((s->send_end!=s->send_next || s->close_requested) && !send_window(s,s->send_next) && now-s->probe_started>=s->probe_interval) {
                    /* An out-of-window ACK elicits the peer's current window
                     * without consuming or dropping queued application data. */
                    (void)wire(s,s->send_next-1,0,0,TCP_FLAG_ACK);
                    s->probe_started=now; s->probe_interval=minimum(TCP_RTO_MAX,s->probe_interval*2);
                }
            }
        }
        if ((s->outstanding || s->send_end!=s->send_unacknowledged || s->close_requested) &&
            now-s->last_received>=TCP_STALL_TIMEOUT) close_socket(s);
    }
}
void tcp_init(void) {
    MUTEX_GUARD(kernel_mutex);
    for (unsigned i=0;i<TCP_MAX_SOCKETS;i++) if (sockets[i].in_use) close_socket(&sockets[i]);
    ipv4_set_protocol_handler(TCP_PROTOCOL,tcp_receive);
}
tcp_socket_t* tcp_connect(uint32_t remote_ip, uint16_t remote_port) {
    MUTEX_GUARD(kernel_mutex);
    if (!unicast_peer(remote_ip) || !remote_port || !ipv4_get_local_ip()) return 0;
    uint16_t port=0;
    for (unsigned trial=0;trial<16384;trial++) {
        uint16_t candidate=next_ephemeral_port++;
        if (next_ephemeral_port<49152) next_ephemeral_port=49152;
        int used=0;
        for (unsigned i=0;i<TCP_MAX_SOCKETS;i++) if (sockets[i].in_use && sockets[i].local_port==candidate) used=1;
        if (!used) { port=candidate; break; }
    }
    if (!port) return 0;
    tcp_socket_t* s=allocate_socket();
    if (!s) return 0;
    s->local_ip=ipv4_get_local_ip(); s->remote_ip=remote_ip;
    s->local_port=port; s->remote_port=remote_port;
    s->send_next=s->send_unacknowledged=new_sequence(); s->send_end=s->send_next+1;
    set_state(s,TCP_SYN_SENT); start_segment(s,0,TCP_FLAG_SYN);
    return s;
}
tcp_socket_t* tcp_listen(uint16_t port) {
    MUTEX_GUARD(kernel_mutex);
    if (!port || !ipv4_get_local_ip()) return 0;
    for (unsigned i=0;i<TCP_MAX_SOCKETS;i++)
        if (sockets[i].in_use && sockets[i].state==TCP_LISTEN && sockets[i].local_port==port) return 0;
    tcp_socket_t* s=allocate_socket();
    if (!s) return 0;
    s->local_ip=ipv4_get_local_ip(); s->local_port=port; set_state(s,TCP_LISTEN);
    return s;
}
void tcp_bind(tcp_socket_t* s, tcp_data_handler_t handler) {
    MUTEX_GUARD(kernel_mutex);
    if (s && s->in_use && !s->detached && s->state!=TCP_CLOSED) { s->handler=handler; drain_receive(s); }
}
void tcp_resume(tcp_socket_t* s) {
    MUTEX_GUARD(kernel_mutex);
    if (!s || !s->in_use || s->state==TCP_CLOSED) return;
    drain_receive(s);
    if (s->in_use && s->state!=TCP_CLOSED && s->advertised_window!=receive_window(s)) acknowledge(s);
}
uint8_t tcp_send(tcp_socket_t* s, const uint8_t* data, uint16_t length) {
    MUTEX_GUARD(kernel_mutex);
    if (!s || !s->in_use || s->detached || s->close_requested ||
        (s->state!=TCP_ESTABLISHED && s->state!=TCP_CLOSE_WAIT) ||
        length>TCP_MAX_PAYLOAD || (!data && length)) return 0;
    if (length>TCP_TX_SIZE-(s->send_end-s->send_unacknowledged)) return 0;
    for (unsigned i=0;i<length;i++) s->tx[(s->send_end+i)&(TCP_TX_SIZE-1u)]=data[i];
    s->send_end+=length; flush(s); return 1;
}
void tcp_disconnect(tcp_socket_t* s) {
    MUTEX_GUARD(kernel_mutex);
    if (!s || !s->in_use) return;
    if (s->state==TCP_LISTEN) {
        for (unsigned i=0;i<TCP_MAX_SOCKETS;i++) if (sockets[i].in_use && sockets[i].parent==s) close_socket(&sockets[i]);
        close_socket(s); return;
    }
    if (s->state==TCP_SYN_SENT || s->state==TCP_SYN_RECEIVED) { close_socket(s); return; }
    s->close_requested=1; flush(s);
}
void tcp_release(tcp_socket_t* s) {
    MUTEX_GUARD(kernel_mutex);
    if (!s || !s->in_use) return;
    s->detached=1; s->handler=0;
    tcp_disconnect(s);
    if (s->state==TCP_CLOSED) s->in_use=0;
    else drain_receive(s);
}
tcp_socket_state_t tcp_get_state(const tcp_socket_t* s) {
    MUTEX_GUARD(kernel_mutex);
    return s && s->in_use ? s->state : TCP_CLOSED;
}
uint32_t tcp_get_remote_ip(const tcp_socket_t* s) {
    MUTEX_GUARD(kernel_mutex);
    return s && s->in_use ? s->remote_ip : 0;
}

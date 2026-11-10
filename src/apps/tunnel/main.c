/* tunnel: expose a local web server through a NixelOS tunnel server.
 *
 * Usage: tunnel [local-port] [server-host[:port]]
 *   defaults: qksrv's port (port= in /etc/quark.cfg, else 8080),
 *             server api.tunnels-test.nixelos.org:80
 *
 * Protocol (see the tunnel server's server.js):
 *   - Open a WebSocket to /tunnel. The server replies with a JSON text message
 *     {"type":"ready","url":"http://tunXXXXX..."}.
 *   - For each visitor the server sends {"type":"connection","id":"<24 hex>"}.
 *     We open a WebSocket to /data/<id> plus a TCP connection to the local
 *     server and copy bytes both ways (binary frames on the WebSocket side).
 *
 * No auth: this is the testing client. Plain HTTP (ws://) only.
 *
 * User TCP sockets are a small system-wide pool (16), shared with qksrv, so only
 * MAX_CONNS visitor connections are forwarded at once; the rest wait in a queue.
 */

#include "../../kernel/types.h"
#include "../../system/user/sys_api.h"

#define DEFAULT_HOST       "signaling.nixeltunnels.space"
#define DEFAULT_SERVER_PORT 80u
#define DEFAULT_LOCAL_PORT  8080u /* qksrv's built-in default */
#define QKSRV_CONFIG        "/etc/quark.cfg"
#define LOCAL_IP            0x7F000001u /* 127.0.0.1, looped back by the kernel */

#define TICKS_PER_SECOND    100u
#define CONNECT_TIMEOUT     (5u * TICKS_PER_SECOND)
#define CLOSE_TIMEOUT       (3u * TICKS_PER_SECOND)
#define QUEUE_MAX_AGE       (9u * TICKS_PER_SECOND) /* server gives up after 10 s */

#define MAX_CONNS   4u
#define MAX_QUEUED  16u
#define OUTQ_SIZE   16384u
#define READ_CHUNK  4096u
#define SEND_CHUNK  1448u
#define HDR_MAX     1024u
#define CTL_MAX     1024u

/* GCC may turn copy loops into calls to these even in freestanding mode. */
void* memcpy(void* dst, const void* src, uint32_t n) {
    uint8_t* d = (uint8_t*)dst; const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dst;
}
void* memset(void* dst, int value, uint32_t n) {
    uint8_t* d = (uint8_t*)dst;
    while (n--) *d++ = (uint8_t)value;
    return dst;
}

/* ------------------------------------------------------------------------ */
/* Output helpers                                                            */

static uint32_t text_length(const char* s) { uint32_t n = 0; while (s[n]) n++; return n; }

static void print_bytes(const char* s, uint32_t n) {
    while (n) {
        uint32_t chunk = n > 256u ? 256u : n;
        sys_write(s, chunk);
        s += chunk; n -= chunk;
    }
}
static void print(const char* s) { print_bytes(s, text_length(s)); }

static void print_number(uint32_t value) {
    char digits[11]; uint32_t count = 0;
    do { digits[count++] = (char)('0' + value % 10u); value /= 10u; } while (value);
    while (count) { char c = digits[--count]; print_bytes(&c, 1); }
}

/* ------------------------------------------------------------------------ */
/* Small utilities                                                           */

static uint32_t rng_state;
static uint32_t random32(void) {
    uint32_t x = rng_state ? rng_state : 0x6D2B79F5u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng_state = x;
    return x;
}

static int append(char* out, uint32_t cap, uint32_t* len, const char* text) {
    while (*text) {
        if (*len + 1u >= cap) return 0;
        out[(*len)++] = *text++;
    }
    out[*len] = '\0';
    return 1;
}

static void base64_encode(const uint8_t* in, uint32_t n, char* out) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t o = 0;
    for (uint32_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < n) v |= in[i + 2];
        out[o++] = table[(v >> 18) & 63];
        out[o++] = table[(v >> 12) & 63];
        out[o++] = i + 1 < n ? table[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? table[v & 63] : '=';
    }
    out[o] = '\0';
}

/* Finds "key":"value" in a flat JSON object. Returns 1 and copies value. */
static int json_string(const char* json, uint32_t len, const char* key,
                       char* out, uint32_t cap) {
    uint32_t key_len = text_length(key);
    for (uint32_t i = 0; i + key_len + 3u < len; i++) {
        if (json[i] != '"') continue;
        uint32_t k = 0;
        while (k < key_len && json[i + 1 + k] == key[k]) k++;
        if (k != key_len || json[i + 1 + k] != '"') continue;
        uint32_t p = i + 2 + key_len;
        while (p < len && (json[p] == ' ' || json[p] == ':')) p++;
        if (p >= len || json[p] != '"') return 0;
        p++;
        uint32_t o = 0;
        while (p < len && json[p] != '"' && o + 1u < cap) out[o++] = json[p++];
        out[o] = '\0';
        return p < len && json[p] == '"';
    }
    return 0;
}

static int tcp_gone(int32_t state) {
    return state < 0 || state == USER_TCP_CLOSED;
}
static int tcp_peer_done(int32_t state) {
    return state < 0 || state == USER_TCP_CLOSED || state == USER_TCP_CLOSE_WAIT;
}

/* ------------------------------------------------------------------------ */
/* Outgoing byte queue (sys_tcp_send takes <= 1448 bytes and may push back)  */

typedef struct {
    uint8_t data[OUTQ_SIZE];
    uint32_t head, count;
} outq_t;

static uint32_t q_space(const outq_t* q) { return OUTQ_SIZE - q->count; }

static int q_push(outq_t* q, const uint8_t* data, uint32_t n) {
    if (n > q_space(q)) return 0;
    for (uint32_t i = 0; i < n; i++)
        q->data[(q->head + q->count + i) % OUTQ_SIZE] = data[i];
    q->count += n;
    return 1;
}

static uint8_t send_buffer[SEND_CHUNK];

/* Sends as much of the queue as the socket accepts right now. */
static void q_flush(int32_t sock, outq_t* q) {
    for (uint32_t rounds = 0; q->count && rounds < 16u; rounds++) {
        uint32_t n = q->count < SEND_CHUNK ? q->count : SEND_CHUNK;
        for (uint32_t i = 0; i < n; i++)
            send_buffer[i] = q->data[(q->head + i) % OUTQ_SIZE];
        if (!sys_tcp_send((uint32_t)sock, send_buffer, n)) return; /* retry later */
        q->head = (q->head + n) % OUTQ_SIZE;
        q->count -= n;
    }
}

/* ------------------------------------------------------------------------ */
/* WebSocket client                                                          */

enum { WS_OP_CONT = 0, WS_OP_TEXT = 1, WS_OP_BINARY = 2,
       WS_OP_CLOSE = 8, WS_OP_PING = 9, WS_OP_PONG = 10 };

typedef struct ws ws_t;
typedef void (*ws_message_fn)(ws_t* ws, const uint8_t* data, uint32_t len);
typedef void (*ws_binary_fn)(void* owner, const uint8_t* data, uint32_t len);

struct ws {
    int32_t sock;
    uint8_t connected;       /* TCP established and upgrade request queued */
    uint8_t open;            /* got "101 Switching Protocols" */
    uint8_t got_close;       /* peer sent a close frame */
    uint8_t sent_close;
    uint8_t failed;
    char path[64];
    char header[HDR_MAX];
    uint32_t header_len;
    /* frame parser */
    uint8_t fh[14];
    uint32_t fh_len, fh_need;
    uint8_t in_payload, opcode, message_opcode, masked;
    uint8_t mask[4];
    uint32_t mask_pos, payload_left;
    uint8_t ctl[CTL_MAX];
    uint32_t ctl_len;
    /* callbacks */
    ws_message_fn on_text;      /* control channel messages */
    ws_binary_fn on_binary;     /* data channel payload */
    void* owner;
    outq_t out;
};

static char server_host[128];
static uint32_t server_ip;
static uint16_t server_port = DEFAULT_SERVER_PORT;
static uint16_t local_port = DEFAULT_LOCAL_PORT;

static int ws_frame(ws_t* ws, uint8_t opcode, const uint8_t* data, uint32_t len) {
    uint8_t header[14];
    uint32_t h = 0;
    header[h++] = (uint8_t)(0x80u | opcode);
    if (len < 126u) {
        header[h++] = (uint8_t)(0x80u | len);
    } else if (len < 65536u) {
        header[h++] = 0x80u | 126u;
        header[h++] = (uint8_t)(len >> 8);
        header[h++] = (uint8_t)len;
    } else {
        header[h++] = 0x80u | 127u;
        for (int i = 0; i < 4; i++) header[h++] = 0;
        header[h++] = (uint8_t)(len >> 24); header[h++] = (uint8_t)(len >> 16);
        header[h++] = (uint8_t)(len >> 8);  header[h++] = (uint8_t)len;
    }
    uint32_t key = random32();
    uint8_t mask[4] = { (uint8_t)key, (uint8_t)(key >> 8),
                        (uint8_t)(key >> 16), (uint8_t)(key >> 24) };
    for (int i = 0; i < 4; i++) header[h++] = mask[i];
    if (q_space(&ws->out) < h + len) return 0;
    q_push(&ws->out, header, h);
    for (uint32_t i = 0; i < len; i++) {
        uint8_t b = (uint8_t)(data[i] ^ mask[i & 3u]);
        q_push(&ws->out, &b, 1);
    }
    return 1;
}

static void ws_send_close(ws_t* ws) {
    if (ws->sent_close || !ws->open) return;
    static const uint8_t normal[2] = { 0x03, 0xE8 }; /* 1000 */
    if (ws_frame(ws, WS_OP_CLOSE, normal, 2)) ws->sent_close = 1;
}

static int ws_start(ws_t* ws, const char* path) {
    ws->sock = sys_tcp_connect(server_ip, server_port);
    if (ws->sock < 0) return 0;
    uint32_t n = 0;
    while (path[n] && n + 1u < sizeof(ws->path)) { ws->path[n] = path[n]; n++; }
    ws->path[n] = '\0';
    return 1;
}

static void ws_queue_upgrade(ws_t* ws) {
    uint8_t nonce[16];
    for (int i = 0; i < 16; i += 4) {
        uint32_t r = random32();
        nonce[i] = (uint8_t)r; nonce[i + 1] = (uint8_t)(r >> 8);
        nonce[i + 2] = (uint8_t)(r >> 16); nonce[i + 3] = (uint8_t)(r >> 24);
    }
    char key[32];
    base64_encode(nonce, 16, key);

    char request[512];
    uint32_t len = 0;
    request[0] = '\0';
    append(request, sizeof(request), &len, "GET ");
    append(request, sizeof(request), &len, ws->path);
    append(request, sizeof(request), &len, " HTTP/1.1\r\nHost: ");
    append(request, sizeof(request), &len, server_host);
    append(request, sizeof(request), &len,
           "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ");
    append(request, sizeof(request), &len, key);
    append(request, sizeof(request), &len,
           "\r\nSec-WebSocket-Version: 13\r\nUser-Agent: nixel-tunnel/0.1 (NixelOS)\r\n\r\n");
    q_push(&ws->out, (const uint8_t*)request, len);
    ws->connected = 1;
}

static void ws_frame_done(ws_t* ws) {
    uint8_t op = ws->opcode == WS_OP_CONT ? ws->message_opcode : ws->opcode;
    if (ws->opcode == WS_OP_PING) {
        ws_frame(ws, WS_OP_PONG, ws->ctl, ws->ctl_len);
    } else if (ws->opcode == WS_OP_CLOSE) {
        ws->got_close = 1;
        if (!ws->sent_close && ws_frame(ws, WS_OP_CLOSE, ws->ctl, ws->ctl_len > 2u ? 2u : ws->ctl_len))
            ws->sent_close = 1;
    } else if (op == WS_OP_TEXT && (ws->fh[0] & 0x80u)) {
        if (ws->on_text) ws->on_text(ws, ws->ctl, ws->ctl_len);
    }
    if (ws->opcode != WS_OP_CONT && ws->opcode < 8u) ws->message_opcode = ws->opcode;
    ws->ctl_len = 0;
    ws->in_payload = 0;
    ws->fh_len = 0;
}

/* Feeds raw bytes from the socket through the frame parser. */
static void ws_feed(ws_t* ws, const uint8_t* data, uint32_t len) {
    uint32_t i = 0;
    while (i < len && !ws->failed) {
        if (!ws->in_payload) {
            ws->fh[ws->fh_len++] = data[i++];
            if (ws->fh_len == 2u) {
                uint32_t l7 = ws->fh[1] & 0x7Fu;
                ws->masked = (ws->fh[1] & 0x80u) != 0;
                ws->fh_need = 2u + (l7 == 126u ? 2u : l7 == 127u ? 8u : 0u) + (ws->masked ? 4u : 0u);
            }
            if (ws->fh_len < 2u || ws->fh_len < ws->fh_need) continue;

            uint32_t l7 = ws->fh[1] & 0x7Fu, p = 2, plen = l7;
            if (l7 == 126u) { plen = (uint32_t)ws->fh[2] << 8 | ws->fh[3]; p = 4; }
            else if (l7 == 127u) {
                if (ws->fh[2] | ws->fh[3] | ws->fh[4] | ws->fh[5]) { ws->failed = 1; return; }
                plen = (uint32_t)ws->fh[6] << 24 | (uint32_t)ws->fh[7] << 16 |
                       (uint32_t)ws->fh[8] << 8 | ws->fh[9];
                p = 10;
            }
            for (int k = 0; k < 4; k++) ws->mask[k] = ws->masked ? ws->fh[p + k] : 0;
            ws->opcode = ws->fh[0] & 0x0Fu;
            ws->payload_left = plen;
            ws->mask_pos = 0;
            ws->in_payload = 1;
            if (!ws->opcode && !ws->message_opcode) ws->message_opcode = WS_OP_BINARY;
            if (!plen) ws_frame_done(ws);
            continue;
        }

        uint32_t n = len - i < ws->payload_left ? len - i : ws->payload_left;
        uint8_t op = ws->opcode == WS_OP_CONT ? ws->message_opcode : ws->opcode;
        if (ws->masked) {
            for (uint32_t k = 0; k < n; k++)
                ((uint8_t*)data)[i + k] ^= ws->mask[(ws->mask_pos + k) & 3u];
            ws->mask_pos += n;
        }
        if (op == WS_OP_BINARY && ws->opcode < 8u) {
            if (ws->on_binary) ws->on_binary(ws->owner, data + i, n);
        } else {
            for (uint32_t k = 0; k < n && ws->ctl_len < CTL_MAX; k++)
                ws->ctl[ws->ctl_len++] = data[i + k];
        }
        i += n;
        ws->payload_left -= n;
        if (!ws->payload_left) ws_frame_done(ws);
    }
}

/* Handles the HTTP upgrade response; leftover bytes go to the frame parser. */
static void ws_feed_handshake(ws_t* ws, const uint8_t* data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        if (ws->header_len + 1u >= HDR_MAX) { ws->failed = 1; return; }
        ws->header[ws->header_len++] = (char)data[i];
        uint32_t h = ws->header_len;
        if (h >= 4u && ws->header[h - 4] == '\r' && ws->header[h - 3] == '\n' &&
            ws->header[h - 2] == '\r' && ws->header[h - 1] == '\n') {
            ws->header[h] = '\0';
            /* "HTTP/1.1 101 ..." */
            if (h < 12u || ws->header[9] != '1' || ws->header[10] != '0' || ws->header[11] != '1') {
                ws->failed = 1;
                return;
            }
            ws->open = 1;
            if (i + 1u < len) ws_feed(ws, data + i + 1u, len - i - 1u);
            return;
        }
    }
}

static uint8_t read_buffer[READ_CHUNK];

/* Reads up to `capacity` bytes. Returns bytes read, 0 if none, -1 if the peer is done. */
static int32_t ws_read(ws_t* ws, uint32_t capacity) {
    if (!capacity) return 0;
    if (capacity > READ_CHUNK) capacity = READ_CHUNK;
    int32_t state = sys_tcp_state((uint32_t)ws->sock);
    int32_t got = sys_tcp_read((uint32_t)ws->sock, read_buffer, capacity);
    if (got > 0) {
        if (ws->open) ws_feed(ws, read_buffer, (uint32_t)got);
        else ws_feed_handshake(ws, read_buffer, (uint32_t)got);
        return got;
    }
    return (got < 0 || tcp_peer_done(state)) ? -1 : 0;
}

/* ------------------------------------------------------------------------ */
/* Forwarded visitor connections                                             */

typedef struct {
    uint8_t active;
    uint8_t closing;
    uint8_t local_eof, ws_eof;
    uint32_t started, close_deadline;
    int32_t local;
    uint8_t local_ready, local_failed;
    outq_t local_out;   /* bytes for the local web server */
    ws_t ws;
} conn_t;

typedef struct { char id[32]; uint32_t queued; } pending_t;

static ws_t control;
static conn_t conns[MAX_CONNS];
static pending_t pending[MAX_QUEUED];
static uint32_t pending_head, pending_count;
static uint8_t tunnel_ready;
static uint8_t warned_local;
static uint8_t local_buffer[READ_CHUNK];

static void conn_to_local(void* owner, const uint8_t* data, uint32_t len) {
    conn_t* c = (conn_t*)owner;
    if (!q_push(&c->local_out, data, len)) c->ws.failed = 1; /* should not happen: reads are bounded */
}

static void conn_release(conn_t* c) {
    if (c->ws.sock >= 0) sys_tcp_close((uint32_t)c->ws.sock);
    if (c->local >= 0) sys_tcp_close((uint32_t)c->local);
    c->ws.sock = -1;
    c->local = -1;
    c->active = 0;
}

static void conn_begin_close(conn_t* c) {
    if (c->closing) return;
    c->closing = 1;
    c->close_deadline = sys_get_ticks() + CLOSE_TIMEOUT;
}

static int conn_start(conn_t* c, const char* id) {
    memset(c, 0, sizeof(*c));
    c->ws.sock = -1;
    c->local = -1;
    char path[64];
    uint32_t len = 0;
    path[0] = '\0';
    append(path, sizeof(path), &len, "/data/");
    append(path, sizeof(path), &len, id);
    if (!ws_start(&c->ws, path)) return 0;
    c->local = sys_tcp_connect(LOCAL_IP, local_port);
    if (c->local < 0) { conn_release(c); return 0; }
    c->ws.on_binary = conn_to_local;
    c->ws.owner = c;
    c->started = sys_get_ticks();
    c->active = 1;
    return 1;
}

static void conn_poll(conn_t* c) {
    uint32_t now = sys_get_ticks();
    int32_t ws_state = sys_tcp_state((uint32_t)c->ws.sock);
    int32_t local_state = sys_tcp_state((uint32_t)c->local);

    /* Connection setup */
    if (!c->ws.connected) {
        if (ws_state == USER_TCP_ESTABLISHED) ws_queue_upgrade(&c->ws);
        else if (tcp_gone(ws_state) || now - c->started > CONNECT_TIMEOUT) { conn_release(c); return; }
    }
    if (!c->local_ready && !c->local_failed) {
        if (local_state == USER_TCP_ESTABLISHED) {
            c->local_ready = 1;
        } else if (tcp_gone(local_state) || now - c->started > CONNECT_TIMEOUT) {
            c->local_failed = 1;
            if (!warned_local) {
                warned_local = 1;
                print("tunnel: cannot reach 127.0.0.1:");
                print_number(local_port);
                print(" - is qksrv running on that port?\n");
            }
        }
    }
    /* Local server refused or timed out. A refusal over loopback arrives long
     * before the WebSocket through the internet opens, so keep waiting for the
     * WebSocket and tell the visitor, instead of leaving the server to time out. */
    if (c->local_failed && !c->closing) {
        if (c->ws.open) {
            static const char bad_gateway[] =
                "HTTP/1.1 502 Bad Gateway\r\nContent-Type: text/plain\r\n"
                "Content-Length: 37\r\nConnection: close\r\n\r\n"
                "The NixelOS web server is not running";
            ws_frame(&c->ws, WS_OP_BINARY, (const uint8_t*)bad_gateway, sizeof(bad_gateway) - 1u);
            ws_send_close(&c->ws);
            c->local_eof = 1;
            conn_begin_close(c);
        } else if (now - c->started > CONNECT_TIMEOUT) {
            conn_release(c);
            return;
        }
    }

    /* Tunnel -> local server */
    if (c->ws.connected && !c->ws_eof && q_space(&c->local_out) >= READ_CHUNK) {
        if (ws_read(&c->ws, READ_CHUNK) < 0) c->ws_eof = 1;
    }
    if (c->ws.failed) { conn_release(c); return; }
    if (c->local_ready) q_flush(c->local, &c->local_out);

    /* Local server -> tunnel (wrapped in binary frames) */
    if (c->ws.open && c->local_ready && !c->local_eof && q_space(&c->ws.out) >= READ_CHUNK + 14u) {
        int32_t got = sys_tcp_read((uint32_t)c->local, local_buffer, READ_CHUNK);
        if (got > 0) ws_frame(&c->ws, WS_OP_BINARY, local_buffer, (uint32_t)got);
        else if (got < 0 || tcp_peer_done(local_state)) c->local_eof = 1;
    }
    if (c->ws.connected) q_flush(c->ws.sock, &c->ws.out);

    /* Shutdown: either side finished, drain what is queued, then close both. */
    if (c->local_eof && c->ws.open) ws_send_close(&c->ws);
    if (c->local_eof || c->ws_eof || c->ws.got_close) conn_begin_close(c);
    if (c->closing) {
        int drained = !c->ws.out.count && (!c->local_ready || !c->local_out.count);
        if (drained || (int32_t)(now - c->close_deadline) > 0) conn_release(c);
    }
}

/* ------------------------------------------------------------------------ */
/* Control channel                                                           */

static void control_message(ws_t* ws, const uint8_t* data, uint32_t len) {
    (void)ws;
    const char* json = (const char*)data;
    char type[24], value[160];
    if (!json_string(json, len, "type", type, sizeof(type))) return;

    if (type[0] == 'r' && type[1] == 'e') { /* ready */
        if (!json_string(json, len, "url", value, sizeof(value))) value[0] = '\0';
        print("Tunnel open: ");
        print(value);
        print(" -> 127.0.0.1:");
        print_number(local_port);
        print("\n");
        tunnel_ready = 1;
    } else if (type[0] == 'c') { /* connection */
        if (!json_string(json, len, "id", value, sizeof(value))) return;
        if (pending_count >= MAX_QUEUED) return; /* server times it out with a 504 */
        pending_t* p = &pending[(pending_head + pending_count) % MAX_QUEUED];
        uint32_t i = 0;
        while (value[i] && i + 1u < sizeof(p->id)) { p->id[i] = value[i]; i++; }
        p->id[i] = '\0';
        p->queued = sys_get_ticks();
        pending_count++;
    } else if (type[0] == 'e') { /* error */
        print("tunnel: server error: ");
        if (json_string(json, len, "message", value, sizeof(value))) print(value);
        print("\n");
    }
}

static void start_pending(void) {
    uint32_t now = sys_get_ticks();
    while (pending_count) {
        pending_t* p = &pending[pending_head];
        if (now - p->queued > QUEUE_MAX_AGE) { /* the visitor already got a 504 */
            pending_head = (pending_head + 1u) % MAX_QUEUED;
            pending_count--;
            continue;
        }
        conn_t* slot = 0;
        for (uint32_t i = 0; i < MAX_CONNS; i++) if (!conns[i].active) { slot = &conns[i]; break; }
        if (!slot || !conn_start(slot, p->id)) return; /* no free connection or socket: try later */
        pending_head = (pending_head + 1u) % MAX_QUEUED;
        pending_count--;
    }
}

/* "192.168.1.5" -> 0xC0A80105; returns 0 if not a dotted IPv4 address. */
static int parse_ipv4(const char* s, uint32_t* out) {
    uint32_t ip = 0;
    for (int part = 0; part < 4; part++) {
        uint32_t value = 0, digits = 0;
        while (*s >= '0' && *s <= '9') { value = value * 10u + (uint32_t)(*s++ - '0'); digits++; }
        if (!digits || digits > 3u || value > 255u) return 0;
        ip = ip << 8 | value;
        if (part < 3 && *s++ != '.') return 0;
    }
    if (*s) return 0;
    *out = ip;
    return 1;
}

static int resolve_server(void) {
    if (parse_ipv4(server_host, &server_ip)) return 1;
    int32_t resolver = sys_dns_start(server_host);
    if (resolver < 0) return 0;
    uint32_t start = sys_get_ticks();
    for (;;) {
        int32_t result = sys_dns_poll((uint32_t)resolver, &server_ip);
        if (result == 1) return 1;
        if (result != 0 || sys_get_ticks() - start > CONNECT_TIMEOUT) return 0;
        sys_yield();
    }
}

/* Uses the same port as qksrv: "port=" in /etc/quark.cfg, else qksrv's default. */
static void load_qksrv_port(void) {
    char buf[512];
    int32_t bytes = sys_fs_read(QKSRV_CONFIG, buf, sizeof(buf) - 1u);
    if (bytes <= 0) return;
    for (int32_t i = 0; i + 5 <= bytes; i++) {
        if ((i && buf[i - 1] != '\n' && buf[i - 1] != '\r' && buf[i - 1] != ' ') ||
            buf[i] != 'p' || buf[i + 1] != 'o' || buf[i + 2] != 'r' ||
            buf[i + 3] != 't' || buf[i + 4] != '=') continue;
        uint32_t port = 0;
        for (int32_t p = i + 5; p < bytes && buf[p] >= '0' && buf[p] <= '9'; p++)
            port = port * 10u + (uint32_t)(buf[p] - '0');
        if (port && port < 65536u) local_port = (uint16_t)port;
        return;
    }
}

static void parse_arguments(const char* args) {
    while (*args == ' ') args++;
    load_qksrv_port(); /* an explicit port below overrides it */
    if (*args >= '0' && *args <= '9') {
        uint32_t port = 0;
        while (*args >= '0' && *args <= '9') port = port * 10u + (uint32_t)(*args++ - '0');
        if (port && port < 65536u) local_port = (uint16_t)port;
        while (*args == ' ') args++;
    }
    uint32_t n = 0;
    if (*args) {
        while (*args && *args != ' ' && *args != ':' && n + 1u < sizeof(server_host))
            server_host[n++] = *args++;
        server_host[n] = '\0';
        if (*args == ':') {
            uint32_t port = 0;
            args++;
            while (*args >= '0' && *args <= '9') port = port * 10u + (uint32_t)(*args++ - '0');
            if (port && port < 65536u) server_port = (uint16_t)port;
        }
    } else {
        const char* d = DEFAULT_HOST;
        while (d[n] && n + 1u < sizeof(server_host)) { server_host[n] = d[n]; n++; }
        server_host[n] = '\0';
    }
}

void user_main(const char* arguments) {
    const char* args = arguments ? arguments : "";
    if (args[0] == '-' && args[1] == '-' && args[2] == 'h') {
        print("Usage: tunnel [local-port] [server-host[:port]]\n"
              "Exposes a local web server (default: qksrv's port from " QKSRV_CONFIG ")\n"
              "through " DEFAULT_HOST ".\n");
        return;
    }
    parse_arguments(args);
    rng_state = sys_get_ticks() * 2654435761u ^ (uint32_t)sys_getpid() << 16;

    print("tunnel: connecting to ");
    print(server_host);
    print("...\n");
    if (!resolve_server()) { print("tunnel: DNS lookup failed\n"); return; }

    memset(&control, 0, sizeof(control));
    control.on_text = control_message;
    if (!ws_start(&control, "/tunnel")) { print("tunnel: no free TCP socket\n"); return; }
    for (uint32_t i = 0; i < MAX_CONNS; i++) { conns[i].ws.sock = -1; conns[i].local = -1; }

    uint32_t start = sys_get_ticks();
    uint8_t detached = 0;
    for (;;) {
        int32_t state = sys_tcp_state((uint32_t)control.sock);
        if (!control.connected) {
            if (state == USER_TCP_ESTABLISHED) ws_queue_upgrade(&control);
            else if (tcp_gone(state) || sys_get_ticks() - start > CONNECT_TIMEOUT) {
                print("tunnel: could not connect to the tunnel server\n");
                break;
            }
        } else {
            int32_t got = 0;
            for (int rounds = 0; rounds < 4 && (got = ws_read(&control, READ_CHUNK)) > 0; rounds++) {}
            if (control.failed) {
                print(control.open ? "tunnel: bad data from server\n"
                                   : "tunnel: server refused the tunnel (not a 101 response)\n");
                if (!control.open && control.header_len) {
                    uint32_t line = 0;
                    while (line < control.header_len && control.header[line] != '\r') line++;
                    print("tunnel: server said: ");
                    print_bytes(control.header, line);
                    print("\n");
                }
                break;
            }
            if (got < 0 || control.got_close) { print("tunnel: tunnel closed by server\n"); break; }
            q_flush(control.sock, &control.out);
            if (!control.open && sys_get_ticks() - start > CONNECT_TIMEOUT) {
                print("tunnel: no response from the tunnel server\n");
                break;
            }
        }

        if (tunnel_ready && !detached) {
            sys_detach(); /* keep running after the terminal returns, like qksrv */
            detached = 1;
        }

        start_pending();
        for (uint32_t i = 0; i < MAX_CONNS; i++) if (conns[i].active) conn_poll(&conns[i]);
        sys_yield();
    }

    for (uint32_t i = 0; i < MAX_CONNS; i++) if (conns[i].active) conn_release(&conns[i]);
    sys_tcp_close((uint32_t)control.sock);
}

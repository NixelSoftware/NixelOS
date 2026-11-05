#include "http.h"
#include "response.h"
#include "../platform/tcp.h"
#include "../platform/tls.h"

#define HTTP_TIMEOUT_TICKS 1200u
#define HTTP_STALLED_POLLS 1000000u
/* The native TCP ABI accepts up to 1448 bytes per send and 4096 per read. */
#define HTTP_SEND_CHUNK 1024u
#define HTTP_READ_CHUNK 2048u
/* This synchronous client has one transaction at a time; no kernel storage
 * or user pointers survive the call. Framing stays entirely in this app. */
static int tls_error;
int browser_http_tls_error(void) { return tls_error; }
void browser_http_free(void *allocation) { nix_http_free(allocation); }
static int valid_text(const char *p, unsigned cap, int host, int path) {
    if (!p) return 0;
    for (unsigned i = 0; i < cap; i++) {
        unsigned char c = (unsigned char)p[i];
        if (!c) return i != 0;
        if (c < (path ? 33u : 32u) || c >= 127) return 0;
        if (host && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '.')) return 0;
        if (path && (c == '#' || c == '\\')) return 0;
    }
    return 0;
}
static int append(char *out, unsigned cap, unsigned *used, const char *text) {
    while (*text) {
        if (*used == cap) return 0;
        out[(*used)++] = *text++;
    }
    return 1;
}
static int number(char *out, unsigned cap, unsigned *used, unsigned value) {
    char digits[11]; unsigned count = 0;
    do { digits[count++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (count) {
        if (*used == cap) return 0;
        out[(*used)++] = digits[--count];
    }
    return 1;
}
static int ipv4(const char *p, unsigned *address) {
    unsigned value = 0;
    for (unsigned i = 0; i < 4; i++) {
        unsigned part = 0, digits = 0;
        while (*p >= '0' && *p <= '9') {
            part = part * 10 + (unsigned)(*p++ - '0'); digits++;
            if (part > 255 || digits > 3) return 0;
        }
        if (!digits || (i < 3 ? *p++ != '.' : *p != 0)) return 0;
        value = (value << 8) | part;
    }
    *address = value; return 1;
}
static int request_scheme(unsigned secure, const char *host, const char *path, const char *accept,
                           unsigned post, const void *body, unsigned body_length,
                           void *output, unsigned capacity, unsigned *length,
                           browser_http_response_t *response, void **allocated) {
    unsigned char *raw_response = 0;
    unsigned raw_capacity = 0;
    tls_error = 0;
    if (length) *length = 0;
    if (response) *response = (browser_http_response_t){0};
    if ((!allocated && (!output || !capacity)) || !length || !response || (post && body_length && !body) ||
        !valid_text(host, BROWSER_HTTP_HOST_MAX, 1, 0) || !valid_text(path, BROWSER_HTTP_PATH_MAX, 0, 1) || path[0] != '/' ||
        !valid_text(accept, BROWSER_HTTP_ACCEPT_MAX, 0, 0)) return BROWSER_HTTP_INVALID;
    if (!post) body_length = 0;
    if (body_length > BROWSER_HTTP_REQUEST_BODY_MAX) return BROWSER_HTTP_LIMIT;
    char request[2048]; unsigned used = 0;
    if (!append(request, sizeof(request), &used, post ? "POST " : "GET ") ||
        !append(request, sizeof(request), &used, path) ||
        !append(request, sizeof(request), &used, " HTTP/1.1\r\nHost: ") ||
        !append(request, sizeof(request), &used, host) ||
        !append(request, sizeof(request), &used, "\r\nUser-Agent: Nixplorer/1.0\r\nAccept: ") ||
        !append(request, sizeof(request), &used, accept) ||
        !append(request, sizeof(request), &used, "\r\nAccept-Encoding: identity\r\nConnection: close\r\n")) return BROWSER_HTTP_LIMIT;
    if (post && (!append(request, sizeof(request), &used, "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: ") ||
                 !number(request, sizeof(request), &used, body_length) || !append(request, sizeof(request), &used, "\r\n"))) return BROWSER_HTTP_LIMIT;
    if (!append(request, sizeof(request), &used, "\r\n") || body_length > sizeof(request) - used) return BROWSER_HTTP_LIMIT;
    for (unsigned i = 0; i < body_length; i++) request[used++] = ((const char*)body)[i];
    if (secure && browser_tls_prepare()) return BROWSER_HTTP_TLS_PLATFORM;
    nixel_tls_client *tls = 0;
    unsigned address;
    if (!ipv4(host, &address) && !nix_tcp_resolve(host, &address)) return BROWSER_HTTP_DNS;
    int handle = nix_tcp_connect(address, secure ? 443 : 80);
    if (handle < 0) return BROWSER_HTTP_CONNECT;
    unsigned socket_handle = (unsigned)handle;
    unsigned started = nix_tcp_ticks(), polls = 0, sent = 0;
    int result = BROWSER_HTTP_CONNECT;
    for (;;) {
        nix_tcp_yield();
        int state = nix_tcp_state((unsigned)handle);
        if (state < 0 || state == NIX_TCP_CLOSED || state == NIX_TCP_CLOSE_WAIT) goto done;
        if (state == NIX_TCP_ESTABLISHED) break;
        if (++polls > HTTP_STALLED_POLLS || (unsigned)(nix_tcp_ticks() - started) > HTTP_TIMEOUT_TICKS) { result = BROWSER_HTTP_TIMEOUT; goto done; }
    }
    if (secure) {
        tls = browser_tls_create(host, &socket_handle);
        if (!tls) { tls_error = nixel_tls_last_error(NULL); result = BROWSER_HTTP_TLS; goto done; }
        started = nix_tcp_ticks(); polls = 0;
        for (;;) {
            int handshake = nixel_tls_handshake(tls);
            if (handshake == 1) break;
            if (handshake != NIXEL_TLS_AGAIN) { result = BROWSER_HTTP_TLS; goto done; }
            if (++polls > HTTP_STALLED_POLLS || (unsigned)(nix_tcp_ticks() - started) > HTTP_TIMEOUT_TICKS) {
                result = BROWSER_HTTP_TIMEOUT; goto done;
            }
            nix_tcp_yield();
        }
    }
    started = nix_tcp_ticks(); polls = 0;
    while (sent < used) {
        nix_tcp_yield();
        if (nix_tcp_state((unsigned)handle) != NIX_TCP_ESTABLISHED) goto done;
        unsigned n = used - sent;
        if (n > HTTP_SEND_CHUNK) n = HTTP_SEND_CHUNK;
        if (tls) {
            int written = nixel_tls_write(tls, request + sent, n);
            if (written != NIXEL_TLS_AGAIN) {
                if (written <= 0 || (unsigned)written > n) { result = BROWSER_HTTP_TLS; goto done; }
                sent += (unsigned)written; started = nix_tcp_ticks();
            }
        } else if (nix_tcp_send((unsigned)handle, request + sent, n) == 1) {
            sent += n; started = nix_tcp_ticks();
        }
        if (++polls > HTTP_STALLED_POLLS || (unsigned)(nix_tcp_ticks() - started) > HTTP_TIMEOUT_TICKS) { result = BROWSER_HTTP_TIMEOUT; goto done; }
    }
    used = 0; started = nix_tcp_ticks(); polls = 0;
    for (;;) {
        if (used == raw_capacity) {
            if (raw_capacity == HTTP_RAW_MAX) { result = BROWSER_HTTP_LIMIT; goto done; }
            unsigned next = raw_capacity ? (raw_capacity > HTTP_RAW_MAX / 2 ? HTTP_RAW_MAX : raw_capacity * 2) : 16384u;
            unsigned char *grown = nix_http_alloc(next);
            /* Retry with a single page of growth when RAM cannot cover doubling. */
            if (!grown && next - raw_capacity > 4096u) {
                next = raw_capacity > HTTP_RAW_MAX - 4096u ? HTTP_RAW_MAX : raw_capacity + 4096u;
                grown = nix_http_alloc(next);
            }
            if (!grown) { result = BROWSER_HTTP_MEMORY; goto done; }
            for (unsigned i = 0; i < used; i++) grown[i] = raw_response[i];
            nix_http_free(raw_response); raw_response = grown; raw_capacity = next;
        }
        unsigned room = raw_capacity - used;
        if (room > HTTP_READ_CHUNK) room = HTTP_READ_CHUNK;
        nix_tcp_yield();
        int tls_eof = 0;
        int received;
        if (tls) {
            received = nixel_tls_read(tls, raw_response + used, room);
            if (received == NIXEL_TLS_AGAIN) received = 0;
            else if (received == 0) tls_eof = 1;
            else if (received < 0) { result = BROWSER_HTTP_TLS; goto done; }
        } else received = nix_tcp_read((unsigned)handle, raw_response + used, room);
        if (received < 0 || (unsigned)received > room) goto done;
        used += (unsigned)received;
        if (received) started = nix_tcp_ticks();
        int state = nix_tcp_state((unsigned)handle);
        if (state < 0) goto done;
        /* Drain queued bytes after FIN before treating an unframed body as EOF. */
        int eof = tls ? tls_eof : !received && (state == NIX_TCP_CLOSE_WAIT || state == NIX_TCP_CLOSED);
        unsigned offset;
        result = browser_http_parse_response(raw_response, used, eof, response, &offset);
        if (result < 0) goto done;
        if (result == 1) {
            if (allocated) {
                /* Transfer the receive allocation rather than keeping two page copies. */
                for (unsigned i = 0; i < response->body_length; i++) raw_response[i] = raw_response[offset + i];
                *allocated = raw_response; raw_response = 0;
                *length = response->body_length; goto done;
            }
            if (response->body_length > capacity) { result = BROWSER_HTTP_LIMIT; goto done; }
            for (unsigned i = 0; i < response->body_length; i++) ((unsigned char*)output)[i] = raw_response[offset + i];
            *length = response->body_length; goto done;
        }
        result = BROWSER_HTTP_CONNECT;
        if (++polls > HTTP_STALLED_POLLS || (unsigned)(nix_tcp_ticks() - started) > HTTP_TIMEOUT_TICKS) { result = BROWSER_HTTP_TIMEOUT; goto done; }
    }
done:
    nix_http_free(raw_response);
    if (tls) {
        if (result == BROWSER_HTTP_TLS) tls_error = nixel_tls_last_error(tls);
        /* Best effort close_notify; tcp_close drains bytes already queued. */
        if (result == 1) (void)nixel_tls_close(tls);
        nixel_tls_client_destroy(tls);
    }
    nix_tcp_close((unsigned)handle);
    return result;
}

int browser_http_request(const char *host, const char *path, const char *accept,
                           unsigned post, const void *body, unsigned body_length,
                           void *output, unsigned capacity, unsigned *length,
                           browser_http_response_t *response) {
    return browser_http_request_scheme(0, host, path, accept, post, body, body_length,
        output, capacity, length, response);
}

int browser_http_request_scheme(unsigned secure, const char *host, const char *path, const char *accept,
                           unsigned post, const void *body, unsigned body_length,
                           void *output, unsigned capacity, unsigned *length,
                           browser_http_response_t *response) {
    return request_scheme(secure, host, path, accept, post, body, body_length,
                          output, capacity, length, response, 0);
}
int browser_http_request_alloc_scheme(unsigned secure, const char *host, const char *path, const char *accept,
                           unsigned post, const void *body, unsigned body_length,
                           void **output, unsigned *length, browser_http_response_t *response) {
    if (!output) return BROWSER_HTTP_INVALID;
    *output = 0;
    return request_scheme(secure, host, path, accept, post, body, body_length,
                          0, 0, length, response, output);
}

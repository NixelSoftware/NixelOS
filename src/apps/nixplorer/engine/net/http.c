#include "http.h"
#include "response.h"
#include "../platform/tcp.h"

#define HTTP_TIMEOUT_TICKS 1200u
#define HTTP_STALLED_POLLS 1000000u
/* The native TCP ABI accepts up to 1448 bytes per send and 4096 per read. */
#define HTTP_SEND_CHUNK 1024u
#define HTTP_READ_CHUNK 2048u
/* This synchronous client has one transaction at a time; no kernel storage
 * or user pointers survive the call. Framing stays entirely in this app. */
static unsigned char raw_response[HTTP_RAW_MAX];
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
int browser_http_request(const char *host, const char *path, const char *accept,
                           unsigned post, const void *body, unsigned body_length,
                           void *output, unsigned capacity, unsigned *length,
                           browser_http_response_t *response) {
    if (length) *length = 0;
    if (response) *response = (browser_http_response_t){0};
    if (!output || !capacity || !length || !response || (post && body_length && !body) ||
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
    unsigned address;
    if (!ipv4(host, &address) && !nix_tcp_resolve(host, &address)) return BROWSER_HTTP_DNS;
    int handle = nix_tcp_connect(address, 80);
    if (handle < 0) return BROWSER_HTTP_CONNECT;
    unsigned started = nix_tcp_ticks(), polls = 0, sent = 0;
    int result = BROWSER_HTTP_CONNECT;
    for (;;) {
        nix_tcp_yield();
        int state = nix_tcp_state((unsigned)handle);
        if (state < 0 || state == NIX_TCP_CLOSED || state == NIX_TCP_CLOSE_WAIT) goto done;
        if (state == NIX_TCP_ESTABLISHED) break;
        if (++polls > HTTP_STALLED_POLLS || (unsigned)(nix_tcp_ticks() - started) > HTTP_TIMEOUT_TICKS) { result = BROWSER_HTTP_TIMEOUT; goto done; }
    }
    started = nix_tcp_ticks(); polls = 0;
    while (sent < used) {
        nix_tcp_yield();
        if (nix_tcp_state((unsigned)handle) != NIX_TCP_ESTABLISHED) goto done;
        unsigned n = used - sent;
        if (n > HTTP_SEND_CHUNK) n = HTTP_SEND_CHUNK;
        if (nix_tcp_send((unsigned)handle, request + sent, n) == 1) { sent += n; started = nix_tcp_ticks(); }
        if (++polls > HTTP_STALLED_POLLS || (unsigned)(nix_tcp_ticks() - started) > HTTP_TIMEOUT_TICKS) { result = BROWSER_HTTP_TIMEOUT; goto done; }
    }
    used = 0; started = nix_tcp_ticks(); polls = 0;
    for (;;) {
        unsigned room = sizeof(raw_response) - used;
        if (!room) { result = BROWSER_HTTP_LIMIT; goto done; }
        if (room > HTTP_READ_CHUNK) room = HTTP_READ_CHUNK;
        nix_tcp_yield();
        int received = nix_tcp_read((unsigned)handle, raw_response + used, room);
        if (received < 0 || (unsigned)received > room) goto done;
        used += (unsigned)received;
        if (received) started = nix_tcp_ticks();
        int state = nix_tcp_state((unsigned)handle);
        if (state < 0) goto done;
        /* Drain queued bytes after FIN before treating an unframed body as EOF. */
        int eof = !received && (state == NIX_TCP_CLOSE_WAIT || state == NIX_TCP_CLOSED);
        unsigned offset;
        result = browser_http_parse_response(raw_response, used, eof, response, &offset);
        if (result < 0) goto done;
        if (result == 1) {
            if (response->body_length > capacity) { result = BROWSER_HTTP_LIMIT; goto done; }
            for (unsigned i = 0; i < response->body_length; i++) ((unsigned char*)output)[i] = raw_response[offset + i];
            *length = response->body_length; goto done;
        }
        result = BROWSER_HTTP_CONNECT;
        if (++polls > HTTP_STALLED_POLLS || (unsigned)(nix_tcp_ticks() - started) > HTTP_TIMEOUT_TICKS) { result = BROWSER_HTTP_TIMEOUT; goto done; }
    }
done:
    nix_tcp_close((unsigned)handle);
    return result;
}

#include "http.h"

static unsigned line_end(const unsigned char *s, unsigned n, unsigned p) {
    while (p + 1 < n && !(s[p] == '\r' && s[p + 1] == '\n')) p++;
    return p + 1 < n ? p : n;
}
static int equal(const unsigned char *s, unsigned n, const char *word) {
    unsigned i = 0;
    for (; i < n && word[i]; i++) {
        unsigned char c = s[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c != (unsigned char)word[i]) return 0;
    }
    return i == n && !word[i];
}
static int hex(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
int browser_http_body(unsigned char *s, unsigned n,
                      unsigned char **body, unsigned *body_length) {
    unsigned p = line_end(s, n, 0), expected = 0;
    int chunked = 0, has_length = 0, has_transfer = 0;
    if (p == n) return 0;
    p += 2;
    for (;;) {
        unsigned end = line_end(s, n, p);
        if (end == n) return 0;
        if (end == p) { p += 2; break; }
        unsigned colon = p;
        while (colon < end && s[colon] != ':') colon++;
        if (colon == end) return 0;
        unsigned v = colon + 1, limit = end;
        while (v < limit && (s[v] == ' ' || s[v] == '\t')) v++;
        while (limit > v && (s[limit - 1] == ' ' || s[limit - 1] == '\t')) limit--;
        if (equal(s + p, colon - p, "transfer-encoding")) {
            if (has_transfer++ || !equal(s + v, limit - v, "chunked")) return 0;
            chunked = 1;
        } else if (equal(s + p, colon - p, "content-encoding")) {
            if (!equal(s + v, limit - v, "identity")) return 0;
        } else if (equal(s + p, colon - p, "content-length")) {
            if (has_length++ || v == limit) return 0;
            for (; v < limit; v++) {
                if (s[v] < '0' || s[v] > '9' || expected > n / 10) return 0;
                expected = expected * 10 + s[v] - '0';
                if (expected > n) return 0;
            }
        }
        p = end + 2;
    }
    *body = s + p;
    if (!chunked) {
        if (has_length && expected > n - p) return 0;
        *body_length = has_length ? expected : n - p;
        return 1;
    }
    if (has_length) return 0;
    unsigned written = p, start = p;
    for (;;) {
        unsigned end = line_end(s, n, p), size = 0, digits = 0;
        if (end == n) return 0;
        while (p < end && hex(s[p]) >= 0) {
            unsigned digit = (unsigned)hex(s[p++]);
            if (size > n / 16) return 0;
            size = size * 16 + digit;
            if (size > n) return 0;
            digits++;
        }
        if (!digits || (p < end && s[p] != ';')) return 0;
        p = end + 2;
        if (!size) {
            /* Consume trailers through the terminating empty line. */
            for (;;) {
                end = line_end(s, n, p);
                if (end == n) return 0;
                if (end == p) { *body_length = written - start; return 1; }
                p = end + 2;
            }
        }
        if (size > n - p || n - p - size < 2) return 0;
        if (s[p + size] != '\r' || s[p + size + 1] != '\n') return 0;
        for (unsigned i = 0; i < size; i++) s[written++] = s[p++];
        p += 2;
    }
}

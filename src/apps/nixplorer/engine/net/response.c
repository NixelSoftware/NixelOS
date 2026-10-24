#include "response.h"

static unsigned line_end(const unsigned char *s, unsigned n, unsigned p) {
    while (p + 1 < n && !(s[p] == '\r' && s[p + 1] == '\n')) p++;
    return p + 1 < n ? p : n;
}
static int equal(const unsigned char *p, unsigned n, const char *text) {
    unsigned i = 0;
    while (i < n && text[i]) {
        unsigned char c = p[i]; if (c >= 'A' && c <= 'Z') c += 32;
        if (c != (unsigned char)text[i]) return 0;
        i++;
    }
    return i == n && !text[i];
}
static int value_copy(char *out, unsigned cap, const unsigned char *p, unsigned n) {
    if (n >= cap) return 0;
    for (unsigned i = 0; i < n; i++) out[i] = (char)p[i];
    out[n] = 0; return 1;
}
static int token(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '!' || c == '#' || c == '$' ||
           c == '%' || c == '&' || c == '\'' || c == '*' || c == '+' ||
           c == '-' || c == '.' || c == '^' || c == '_' || c == '`' || c == '|' || c == '~';
}
static int header(const unsigned char *s, unsigned start, unsigned end,
                    unsigned *colon, unsigned *value, unsigned *limit) {
    unsigned c = start;
    while (c < end && token(s[c])) c++;
    if (c == start || c == end || s[c] != ':') return 0;
    for (unsigned i = c + 1; i < end; i++)
        if ((s[i] < 32 && s[i] != '\t') || s[i] == 127) return 0;
    unsigned v = c + 1, l = end;
    while (v < l && (s[v] == ' ' || s[v] == '\t')) v++;
    while (l > v && (s[l - 1] == ' ' || s[l - 1] == '\t')) l--;
    *colon = c; *value = v; *limit = l; return 1;
}
static int hex(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
int browser_http_parse_response(unsigned char *s, unsigned n, int eof,
                          browser_http_response_t *out, unsigned *body_offset) {
    unsigned base = 0, body = 0, expected = 0;
    int chunked = 0, has_length = 0;
    for (unsigned interim = 0; ; interim++) {
        if (interim == 5) return BROWSER_HTTP_PROTOCOL;
        unsigned end = line_end(s, n, base);
        if (end == n) return n >= HTTP_HEADER_MAX ? BROWSER_HTTP_LIMIT : eof ? BROWSER_HTTP_PROTOCOL : 0;
        if (end - base < 12 || !(equal(s + base, 8, "http/1.1") || equal(s + base, 8, "http/1.0")) ||
            s[base + 8] != ' ' || s[base + 9] < '1' || s[base + 9] > '5' ||
            s[base + 10] < '0' || s[base + 10] > '9' || s[base + 11] < '0' || s[base + 11] > '9' ||
            (end - base > 12 && s[base + 12] != ' ')) return BROWSER_HTTP_PROTOCOL;
        out->status = (s[base + 9] - '0') * 100u + (s[base + 10] - '0') * 10u + s[base + 11] - '0';
        out->location[0] = out->content_type[0] = 0;
        int has_transfer = 0, has_location = 0;
        chunked = has_length = 0; expected = 0;
        unsigned p = end + 2;
        for (;;) {
            end = line_end(s, n, p);
            if (end == n) return n >= HTTP_HEADER_MAX ? BROWSER_HTTP_LIMIT : eof ? BROWSER_HTTP_PROTOCOL : 0;
            if (end + 2 > HTTP_HEADER_MAX) return BROWSER_HTTP_LIMIT;
            if (end == p) { body = end + 2; break; }
            unsigned colon, value, limit;
            if (!header(s, p, end, &colon, &value, &limit)) return BROWSER_HTTP_PROTOCOL;
            if (equal(s + p, colon - p, "content-length")) {
                if (has_length++ || value == limit) return BROWSER_HTTP_PROTOCOL;
                for (unsigned i = value; i < limit; i++) {
                    if (s[i] < '0' || s[i] > '9') return BROWSER_HTTP_PROTOCOL;
                    unsigned digit = s[i] - '0';
                    if (expected > (BROWSER_HTTP_RESPONSE_BODY_MAX - digit) / 10) return BROWSER_HTTP_LIMIT;
                    expected = expected * 10 + digit;
                }
            } else if (equal(s + p, colon - p, "transfer-encoding")) {
                if (has_transfer++ || !equal(s + value, limit - value, "chunked")) return BROWSER_HTTP_ENCODING;
                chunked = 1;
            } else if (equal(s + p, colon - p, "content-encoding")) {
                if (!equal(s + value, limit - value, "identity")) return BROWSER_HTTP_ENCODING;
            } else if (equal(s + p, colon - p, "location")) {
                if (has_location++) return BROWSER_HTTP_PROTOCOL;
                if (!value_copy(out->location, sizeof(out->location), s + value, limit - value)) return BROWSER_HTTP_LIMIT;
            } else if (equal(s + p, colon - p, "content-type")) {
                if (!value_copy(out->content_type, sizeof(out->content_type), s + value, limit - value)) return BROWSER_HTTP_LIMIT;
            }
            p = end + 2;
        }
        if (has_length && chunked) return BROWSER_HTTP_PROTOCOL;
        if (out->status >= 200) break;
        if (out->status == 101 || chunked || has_length) return BROWSER_HTTP_PROTOCOL;
        base = body;
    }
    *body_offset = body;
    if (out->status == 204 || out->status == 304) { out->body_length = 0; return 1; }
    if (!chunked) {
        if (has_length && n - body < expected) return eof ? BROWSER_HTTP_PROTOCOL : 0;
        if (!has_length && !eof) return 0;
        unsigned size = has_length ? expected : n - body;
        if (size > BROWSER_HTTP_RESPONSE_BODY_MAX) return BROWSER_HTTP_LIMIT;
        out->body_length = size; return 1;
    }
    unsigned p = body, total = 0;
    for (;;) {
        unsigned end = line_end(s, n, p), size = 0, digits = 0;
        if (end == n) return eof ? BROWSER_HTTP_PROTOCOL : 0;
        while (p < end && hex(s[p]) >= 0) {
            unsigned digit = (unsigned)hex(s[p++]);
            if (size > (BROWSER_HTTP_RESPONSE_BODY_MAX - digit) / 16) return BROWSER_HTTP_LIMIT;
            size = size * 16 + digit; digits++;
        }
        if (!digits || (p < end && s[p] != ';')) return BROWSER_HTTP_PROTOCOL;
        for (; p < end; p++) if (s[p] < 32 || s[p] == 127) return BROWSER_HTTP_PROTOCOL;
        p = end + 2;
        if (!size) {
            unsigned trailers = p;
            for (;;) {
                end = line_end(s, n, p);
                if (end == n) return eof ? BROWSER_HTTP_PROTOCOL : 0;
                if (end - trailers > HTTP_HEADER_MAX) return BROWSER_HTTP_LIMIT;
                if (end == p) break;
                unsigned colon, value, limit;
                if (!header(s, p, end, &colon, &value, &limit)) return BROWSER_HTTP_PROTOCOL;
                p = end + 2;
            }
            break;
        }
        if (size > BROWSER_HTTP_RESPONSE_BODY_MAX - total) return BROWSER_HTTP_LIMIT;
        if (size > n - p || n - p - size < 2) return eof ? BROWSER_HTTP_PROTOCOL : 0;
        if (s[p + size] != '\r' || s[p + size + 1] != '\n') return BROWSER_HTTP_PROTOCOL;
        total += size; p += size + 2;
    }
    /* All chunk boundaries and trailers are valid: compact exactly once. */
    p = body; unsigned written = body;
    for (;;) {
        unsigned end = line_end(s, n, p), size = 0;
        while (p < end && hex(s[p]) >= 0) size = size * 16 + (unsigned)hex(s[p++]);
        p = end + 2;
        if (!size) break;
        for (unsigned i = 0; i < size; i++) s[written++] = s[p++];
        p += 2;
    }
    out->body_length = total; return 1;
}

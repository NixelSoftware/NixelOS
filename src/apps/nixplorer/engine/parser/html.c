#include "html.h"
#include "../font/font.h"
#include "entities.inc"

static int space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}
static int alnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}
static char lower(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

unsigned browser_html_reference(const char *text, const char *end,
                                int attribute, unsigned *codepoint) {
    if (text >= end || *text != '&') return 0;
    const char *p = text + 1;
    if (p < end && *p == '#') {
        unsigned value = 0, base = 10, digits = 0;
        int overflow = 0;
        p++;
        if (p < end && (*p == 'x' || *p == 'X')) { base = 16; p++; }
        while (p < end) {
            unsigned digit;
            if (*p >= '0' && *p <= '9') digit = *p - '0';
            else if (lower(*p) >= 'a' && lower(*p) <= 'f') digit = lower(*p) - 'a' + 10;
            else break;
            if (digit >= base) break;
            if (value > (0x10ffffu - digit) / base) overflow = 1;
            if (!overflow) value = value * base + digit;
            digits++; p++;
        }
        if (!digits) return 0;
        if (p < end && *p == ';') p++;
        if (overflow || !value || (value >= 0xd800 && value <= 0xdfff)) value = 0xfffd;
        /* HTML numeric references map the C1 range through Windows-1252. */
        static const unsigned c1[32] = {
            0x20ac,0x81,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,
            0x2c6,0x2030,0x160,0x2039,0x152,0x8d,0x17d,0x8f,
            0x90,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,
            0x2dc,0x2122,0x161,0x203a,0x153,0x9d,0x17e,0x178
        };
        if (value >= 0x80 && value <= 0x9f) value = c1[value - 0x80];
        *codepoint = value;
        return (unsigned)(p - text);
    }
    unsigned best = 0, value = 0;
    for (unsigned i = 0; i < sizeof(entity_values) / sizeof(entity_values[0]); i++) {
        const char *name = entity_names[i];
        unsigned n = 0;
        while (n < (unsigned)(end - p) && name[n] && p[n] == name[n]) n++;
        if (name[n] || n <= best) continue;
        if (attribute && name[n - 1] != ';' && n < (unsigned)(end - p) &&
            (alnum(p[n]) || p[n] == '=')) continue;
        best = n; value = entity_values[i];
    }
    if (!best) return 0;
    *codepoint = value;
    return best + 1;
}

const char *browser_html_tag_end(const char *tag, const char *end) {
    char quote = 0;
    for (; tag < end; tag++) {
        if (quote) { if (*tag == quote) quote = 0; }
        else if (*tag == '\'' || *tag == '"') quote = *tag;
        else if (*tag == '>') return tag;
    }
    return end;
}

int browser_html_attribute(const char *tag, const char *end,
                           const char *name, char *out, unsigned capacity) {
    if (!capacity) return 0;
    out[0] = 0;
    const char *p = tag;
    /* Skip the element name. */
    while (p < end && !space(*p) && *p != '>' && *p != '/') p++;
    while (p < end && *p != '>') {
        while (p < end && (space(*p) || *p == '/')) p++;
        if (p == end || *p == '>') break;
        const char *start = p;
        while (p < end && !space(*p) && *p != '=' && *p != '>' && *p != '/') p++;
        unsigned n = (unsigned)(p - start), k = 0;
        while (k < n && name[k] && lower(start[k]) == lower(name[k])) k++;
        int match = k == n && !name[k];
        /* Ensure progress for malformed attribute names. */
        if (p == start) { p++; continue; }
        while (p < end && space(*p)) p++;
        const char *value = p, *limit = p;
        if (p < end && *p == '=') {
            p++;
            while (p < end && space(*p)) p++;
            char quote = 0;
            if (p < end && (*p == '\'' || *p == '"')) quote = *p++;
            value = p;
            while (p < end && (quote ? *p != quote : !space(*p) && *p != '>')) p++;
            limit = p;
            if (quote && p < end) p++;
        }
        if (!match) continue;
        unsigned used = 0;
        while (value < limit && used + 1 < capacity) {
            unsigned cp, consumed = browser_html_reference(value, limit, 1, &cp);
            out[used++] = consumed ? browser_font_character(cp) : *value;
            value += consumed ? consumed : 1;
        }
        out[used] = 0;
        return 1; /* Presence also represents a boolean attribute. */
    }
    return 0;
}

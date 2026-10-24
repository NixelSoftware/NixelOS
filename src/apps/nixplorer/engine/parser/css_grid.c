#include "css_grid.h"

static int space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static char lower(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static const char *skip(const char *p, const char *end) {
    for (;;) {
        while (p < end && space(*p)) p++;
        if (end - p < 2 || p[0] != '/' || p[1] != '*') return p;
        p += 2;
        while (end - p >= 2 && !(p[0] == '*' && p[1] == '/')) p++;
        if (end - p < 2) return end;
        p += 2;
    }
}
static int word(const char *p, const char *end, const char *s) {
    while (p < end && *s && lower(*p) == *s) { p++; s++; }
    return p == end && !*s;
}
static int track(const char *p, const char *end, browser_grid_track *out) {
    if (word(p, end, "auto")) { out->kind = BROWSER_TRACK_AUTO; out->value = 0; return 1; }
    const char *start = p;
    int whole = 0, fraction = 0, scale = 1, digits = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        whole = whole * 10 + *p++ - '0'; digits++;
        if (whole > 4096) return 0;
    }
    if (p < end && *p == '.') {
        const char *decimal = ++p;
        while (p < end && *p >= '0' && *p <= '9') {
            if (scale == 1000) return 0;
            fraction = fraction * 10 + *p++ - '0'; scale *= 10; digits++;
        }
        if (p == decimal) return 0;
    }
    if (!digits || p == start) return 0;
    if (word(p, end, "fr")) {
        if (whole > 100 || (whole == 100 && fraction)) return 0;
        out->kind = BROWSER_TRACK_FR; out->value = whole * 1000 + fraction * 1000 / scale;
    } else if (word(p, end, "%")) {
        if (whole > 100 || fraction) return 0;
        out->kind = BROWSER_TRACK_PERCENT; out->value = whole;
    } else if (word(p, end, "px") || (p == end && whole == 0 && !fraction)) {
        if (fraction) return 0;
        out->kind = BROWSER_TRACK_PX; out->value = whole;
    } else return 0;
    return 1;
}
static int list(const char **cursor, const char *end, browser_grid_track *out, int *count, unsigned depth) {
    const char *p = *cursor;
    while ((p = skip(p, end)) < end && *p != ')') {
        const char *token = p;
        while (p < end && !space(*p) && *p != '(' && *p != ')') {
            if (end - p >= 2 && p[0] == '/' && p[1] == '*') break;
            p++;
        }
        if (p < end && *p == '(' && word(token, p, "repeat")) {
            if (depth == 2) return 0;
            p = skip(p + 1, end);
            int repetitions = 0;
            const char *number = p;
            while (p < end && *p >= '0' && *p <= '9') {
                repetitions = repetitions * 10 + *p++ - '0';
                if (repetitions > BROWSER_GRID_TRACKS) return 0;
            }
            if (p == number || !repetitions) return 0;
            p = skip(p, end);
            if (p == end || *p++ != ',') return 0;
            browser_grid_track repeated[BROWSER_GRID_TRACKS]; int n = 0;
            if (!list(&p, end, repeated, &n, depth + 1) || !n || p == end || *p++ != ')') return 0;
            if (repetitions * n > BROWSER_GRID_TRACKS - *count) return 0;
            for (int i = 0; i < repetitions; i++) for (int j = 0; j < n; j++) out[(*count)++] = repeated[j];
        } else {
            if (*count == BROWSER_GRID_TRACKS || token == p || !track(token, p, &out[*count])) return 0;
            (*count)++;
        }
        if (p < end && *p != ')' && !space(*p) && !(end - p >= 2 && p[0] == '/' && p[1] == '*')) return 0;
    }
    *cursor = p;
    return 1;
}
int browser_css_grid_tracks(const char *start, const char *end, browser_grid_track *tracks, int *count) {
    const char *p = skip(start, end), *limit = end;
    while (limit > p && space(limit[-1])) limit--;
    if (word(p, limit, "none") || (end - p >= 4 && word(p, p + 4, "none") && skip(p + 4, end) == end)) { *count = 0; return 1; }
    browser_grid_track parsed[BROWSER_GRID_TRACKS]; int n = 0;
    if (!list(&p, end, parsed, &n, 0) || p != end || !n) return 0;
    for (int i = 0; i < n; i++) tracks[i] = parsed[i];
    *count = n;
    return 1;
}

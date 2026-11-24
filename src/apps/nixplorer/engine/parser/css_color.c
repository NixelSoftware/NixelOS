#include "css_color.h"
#include "colors.inc"

static char lower(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f'; }
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
static int equal(const char *p, const char *end, const char *name) {
    while (p < end && *name && lower(*p) == *name) { p++; name++; }
    return p == end && !*name;
}
static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = lower(c); return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
static int number(const char **cursor, const char *end, int *out) {
    const char *p = *cursor;
    int sign = 1, whole = 0, fraction = 0, scale = 1, digits = 0;
    if (p < end && (*p == '+' || *p == '-')) { if (*p == '-') sign = -1; p++; }
    while (p < end && *p >= '0' && *p <= '9') {
        whole = whole * 10 + *p++ - '0'; digits++;
        if (whole > 100000) return 0;
    }
    if (p < end && *p == '.') {
        const char *start = ++p;
        while (p < end && *p >= '0' && *p <= '9') {
            if (scale < 1000) { fraction = fraction * 10 + *p - '0'; scale *= 10; }
            p++; digits++;
        }
        if (p == start) return 0;
    }
    if (!digits) return 0;
    *out = sign * (whole * 1000 + fraction * 1000 / scale); *cursor = p; return 1;
}
static int clamp(int value, int maximum) { return value < 0 ? 0 : value > maximum ? maximum : value; }
static int absolute(int value) { return value < 0 ? -value : value; }
unsigned browser_css_blend(unsigned rgb, unsigned alpha, unsigned backdrop) {
    unsigned result = 0;
    if (alpha > 255) alpha = 255;
    for (unsigned shift = 0; shift <= 16; shift += 8) {
        unsigned channel = (((rgb >> shift) & 255) * alpha + ((backdrop >> shift) & 255) * (255 - alpha) + 127) / 255;
        result |= channel << shift;
    }
    return result;
}
static int parse_color(const char *, const char *, browser_css_color *, unsigned);
static const char *comma_at(const char *p, const char *end) {
    unsigned nesting = 0;
    for (; p < end; p++) {
        if (*p == '(') nesting++;
        else if (*p == ')') { if (!nesting) return end; nesting--; }
        else if (*p == ',' && !nesting) return p;
    }
    return end;
}
static int mix_stop(const char *p, const char *end, browser_css_color *color, int *weight, unsigned depth) {
    while (end > p && space(end[-1])) end--;
    *weight = -1;
    p = skip(p, end);
    const char *leading = p;
    int amount;
    if (number(&leading, end, &amount) && leading < end && *leading == '%') {
        leading++;
        if (amount < 0 || amount > 100000 || leading == end || !space(*leading)) return 0;
        *weight = amount;
        return parse_color(leading, end, color, depth + 1) && color->rgb != BROWSER_CSS_CURRENT_COLOR;
    }
    if (end > p && end[-1] == '%') {
        const char *start = end - 1;
        while (start > p && !space(start[-1])) start--;
        const char *number_end = start;
        if (!number(&number_end, end - 1, weight) || number_end != end - 1 || *weight < 0 || *weight > 100000) return 0;
        end = start;
    }
    return parse_color(p, end, color, depth + 1) && color->rgb != BROWSER_CSS_CURRENT_COLOR;
}
static int mix_color(const char *p, const char *end, browser_css_color *out, unsigned depth) {
    p = skip(p, end);
    const char *first = comma_at(p, end), *mode_end = first;
    while (mode_end > p && space(mode_end[-1])) mode_end--;
    if (end == first || mode_end - p < 4 || !equal(p, p + 2, "in") || !space(p[2])) return 0;
    p = skip(p + 2, mode_end);
    if (!equal(p, mode_end, "srgb")) return 0;
    p = first + 1;
    const char *second = comma_at(p, end);
    browser_css_color a, b; int x, y;
    if (second == end || !mix_stop(p, second, &a, &x, depth) || !mix_stop(second + 1, end, &b, &y, depth)) return 0;
    if (x < 0 && y < 0) x = y = 50000;
    else if (x < 0) x = 100000 - y;
    else if (y < 0) y = 100000 - x;
    unsigned sum = (unsigned)(x + y);
    if (!sum) return 0;
    unsigned weight = ((unsigned)x * 10000u + sum / 2) / sum;
    unsigned aw = a.alpha * weight, bw = b.alpha * (10000u - weight), alpha = aw + bw;
    out->rgb = 0;
    for (unsigned shift = 0; shift <= 16; shift += 8) {
        unsigned channel = alpha ? ((((a.rgb >> shift) & 255) * aw + ((b.rgb >> shift) & 255) * bw + alpha / 2) / alpha) : 0;
        out->rgb |= channel << shift;
    }
    unsigned multiplier = sum < 100000 ? (sum + 50) / 100 : 1000;
    out->alpha = (alpha * multiplier + 5000000u) / 10000000u;
    return 1;
}
static int parse_color(const char *input, const char *end, browser_css_color *out, unsigned depth) {
    if (depth > 4) return 0;
    const char *p = skip(input, end);
    while (end > p && space(end[-1])) end--;
    browser_css_color result = {0, 255};
    if (end - p > 12 && equal(p, p + 11, "light-dark(") && end[-1] == ')') {
        const char *split = p + 11; unsigned nested = 0;
        while (split < end - 1) { if (*split == '(') nested++; else if (*split == ')' && nested) nested--; else if (*split == ',' && !nested) break; split++; }
        browser_css_color dark;
        if (split == end - 1 || !parse_color(p + 11, split, &result, depth + 1) || !parse_color(split + 1, end - 1, &dark, depth + 1)) return 0;
        /* The native browser currently uses a light colour scheme. */
        *out = result; return 1;
    }
    if (end - p >= 11 && equal(p, p + 10, "color-mix(") && end[-1] == ')')
        return mix_color(p + 10, end - 1, out, depth);
    if (end - p >= 7 && equal(p, p + 6, "color(") && end[-1] == ')') {
        p = skip(p + 6, --end);
        const char *mode = p;
        while (p < end && !space(*p)) p++;
        if (!equal(mode, p, "srgb")) return 0;
        for (int i = 0; i < 3; i++) {
            const char *next = skip(p, end);
            if (next == p) return 0;
            /* Keep channels signed until clipping to the native sRGB gamut. */
            int channel; const char *cursor = skip(p, end);
            if (!number(&cursor, end, &channel)) return 0;
            int maximum = 1000;
            if (cursor < end && *cursor == '%') { maximum = 100000; cursor++; }
            result.rgb = (result.rgb << 8) | (unsigned)((clamp(channel, maximum) * 255 + maximum / 2) / maximum);
            p = cursor;
        }
        result.alpha = 255; p = skip(p, end);
        if (p < end && *p == '/') {
            p = skip(p + 1, end); int alpha;
            if (!number(&p, end, &alpha)) return 0;
            int maximum = 1000;
            if (p < end && *p == '%') { maximum = 100000; p++; }
            result.alpha = (unsigned)(clamp(alpha, maximum) * 255 + maximum / 2) / (unsigned)maximum;
        }
        if (skip(p, end) != end) return 0;
        *out = result; return 1;
    }
    if (equal(p, end, "transparent")) { result.alpha = 0; *out = result; return 1; }
    if (equal(p, end, "currentcolor")) { result.rgb = BROWSER_CSS_CURRENT_COLOR; *out = result; return 1; }
    if (p < end && *p == '#') {
        unsigned count = (unsigned)(end - ++p);
        if (count != 3 && count != 4 && count != 6 && count != 8) return 0;
        unsigned bytes[4] = {0, 0, 0, 255}, n = count < 5 ? count : count / 2;
        for (unsigned i = 0; i < n; i++) {
            int value = hex(*p++); if (value < 0) return 0;
            if (count < 5) bytes[i] = (unsigned)value * 17;
            else { int second = hex(*p++); if (second < 0) return 0; bytes[i] = (unsigned)value * 16 + (unsigned)second; }
        }
        result.rgb = (bytes[0] << 16) | (bytes[1] << 8) | bytes[2]; result.alpha = bytes[3]; *out = result; return 1;
    }
    for (unsigned i = 0; i < sizeof(named_colors) / sizeof(named_colors[0]); i++)
        if (equal(p, end, named_colors[i].name)) { result.rgb = named_colors[i].rgb; *out = result; return 1; }
    const char *function = p;
    while (p < end && lower(*p) >= 'a' && lower(*p) <= 'z') p++;
    int hsl = equal(function, p, "hsl") || equal(function, p, "hsla");
    int hwb = equal(function, p, "hwb");
    if ((!hsl && !hwb && !equal(function, p, "rgb") && !equal(function, p, "rgba")) || p == end || *p++ != '(' || end[-1] != ')') return 0;
    end--; int values[3], percentages[3] = {0}, comma = 0;
    for (int i = 0; i < 3; i++) {
        p = skip(p, end);
        if (!number(&p, end, &values[i])) return 0;
        if (p < end && *p == '%') { percentages[i] = 1; p++; }
        if ((hsl || hwb) && i == 0) {
            if (percentages[i]) return 0;
            const char *unit = p;
            while (p < end && lower(*p) >= 'a' && lower(*p) <= 'z') p++;
            if (equal(unit, p, "turn")) values[i] = (values[i] % 1000) * 360;
            else if (unit != p && !equal(unit, p, "deg")) return 0;
        } else if ((hsl || hwb) && !percentages[i]) return 0;
        const char *next = skip(p, end);
        if (i < 2) {
            if (!i) comma = next < end && *next == ',';
            if (comma) { if (next == end || *next != ',') return 0; p = next + 1; }
            else { if (next == p || (next < end && *next == ',')) return 0; p = next; }
        } else p = next;
    }
    if (hwb && comma) return 0;
    if (comma && !hsl && (percentages[0] != percentages[1] || percentages[1] != percentages[2])) return 0;
    if (p < end) {
        if (*p != (comma ? ',' : '/')) return 0;
        p = skip(p + 1, end); int alpha;
        if (!number(&p, end, &alpha)) return 0;
        int percent = p < end && *p == '%'; if (percent) p++;
        int maximum = percent ? 100000 : 1000;
        result.alpha = (unsigned)(clamp(alpha, maximum) * 255 + maximum / 2) / (unsigned)maximum;
        p = skip(p, end);
    }
    if (p != end) return 0;
    if (hwb) {
        int hue = values[0] % 360000; if (hue < 0) hue += 360000;
        int white = clamp(values[1], 100000) / 100, black = clamp(values[2], 100000) / 100;
        int channels[3] = {0}, sector = hue / 60000;
        int x = 1000 - absolute((hue % 120000) / 60 - 1000);
        if (sector == 0) { channels[0] = 1000; channels[1] = x; }
        else if (sector == 1) { channels[0] = x; channels[1] = 1000; }
        else if (sector == 2) { channels[1] = 1000; channels[2] = x; }
        else if (sector == 3) { channels[1] = x; channels[2] = 1000; }
        else if (sector == 4) { channels[0] = x; channels[2] = 1000; }
        else { channels[0] = 1000; channels[2] = x; }
        for (int i = 0; i < 3; i++) {
            int channel = white + black >= 1000 ? (white * 255 + (white + black) / 2) / (white + black) :
                (channels[i] * (1000 - white - black) * 255 / 1000 + white * 255 + 500) / 1000;
            result.rgb = (result.rgb << 8) | (unsigned)channel;
        }
    } else if (hsl) {
        int hue = values[0] % 360000; if (hue < 0) hue += 360000;
        int saturation = clamp(values[1], 100000) / 100, light = clamp(values[2], 100000) / 100;
        int chroma = (1000 - absolute(2 * light - 1000)) * saturation / 1000;
        int x = chroma * (1000 - absolute((hue % 120000) / 60 - 1000)) / 1000, m = light - chroma / 2;
        int r = 0, g = 0, b = 0;
        switch (hue / 60000) {
            case 0: r = chroma; g = x; break; case 1: r = x; g = chroma; break;
            case 2: g = chroma; b = x; break; case 3: g = x; b = chroma; break;
            case 4: r = x; b = chroma; break; default: r = chroma; b = x; break;
        }
        result.rgb = (unsigned)((r + m) * 255 + 500) / 1000 << 16 |
                     (unsigned)((g + m) * 255 + 500) / 1000 << 8 |
                     (unsigned)((b + m) * 255 + 500) / 1000;
    } else {
        for (int i = 0; i < 3; i++) {
            int maximum = percentages[i] ? 100000 : 255000;
            int channel = percentages[i] ? (clamp(values[i], maximum) * 255 + 50000) / 100000 : (clamp(values[i], maximum) + 500) / 1000;
            result.rgb = (result.rgb << 8) | (unsigned)channel;
        }
    }
    *out = result; return 1;
}

int browser_css_parse_color(const char *p, const char *end, browser_css_color *out) {
    if (!p || !out || !end || end < p) return 0;
    char value[256]; unsigned count = 0;
    while (p < end) {
        if (end - p >= 2 && p[0] == '/' && p[1] == '*') {
            p += 2;
            while (end - p >= 2 && !(p[0] == '*' && p[1] == '/')) p++;
            p = end - p >= 2 ? p + 2 : end;
            if (count == sizeof(value)) return 0;
            value[count++] = ' ';
        } else {
            if (count == sizeof(value)) return 0;
            value[count++] = *p++;
        }
    }
    return parse_color(value, value + count, out, 0);
}

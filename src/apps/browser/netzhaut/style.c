/* Bounded native cascade adapter, informed by Netzhaut's nh-css split between
 * selector parsing and Properties/Cascade.c. Supports a deliberate CSS subset. */
#include "style.h"
#include "html.h"

static int space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static char lower(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int ident(char c) {
    return (lower(c) >= 'a' && lower(c) <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_';
}
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
static const char *trim(const char *p, const char *end) {
    while (end > p && space(end[-1])) end--;
    return end;
}
static int equal(const char *p, const char *end, const char *s, int fold) {
    while (p < end && *s) {
        if ((fold ? lower(*p) : *p) != (fold ? lower(*s) : *s)) return 0;
        p++; s++;
    }
    return p == end && !*s;
}
/* Find a delimiter outside strings, comments, and function arguments. */
static const char *find(const char *p, const char *end, char delim) {
    char quote = 0; unsigned parens = 0;
    for (; p < end; p++) {
        if (quote) {
            if (*p == '\\' && p + 1 < end) p++;
            else if (*p == quote) quote = 0;
        } else if (*p == '\'' || *p == '"') quote = *p;
        else if (end - p >= 2 && p[0] == '/' && p[1] == '*') {
            p = skip(p, end);
            if (p == end) return end;
            p--;
        } else if (*p == '(' || *p == '[') parens++;
        else if ((*p == ')' || *p == ']') && parens) parens--;
        else if (!parens && *p == delim) return p;
    }
    return end;
}
static void add_sheet(browser_stylesheet *sheet, const char *p, const char *end) {
    while ((p = skip(p, end)) < end) {
        if (*p == '@') {
            const char *brace = find(p, end, '{'), *semi = find(p, end, ';');
            if (semi < brace) { p = semi + 1; continue; }
            if (brace == end) return;
            unsigned depth = 1;
            p = brace + 1;
            /* Unsupported conditional blocks are skipped as a whole. */
            while (p < end && depth) {
                const char *open = find(p, end, '{'), *close = find(p, end, '}');
                if (open < close) { depth++; p = open + 1; }
                else if (close < end) { depth--; p = close + 1; }
                else return;
            }
            continue;
        }
        const char *brace = find(p, end, '{');
        if (brace == end) return;
        const char *close = find(brace + 1, end, '}');
        if (close == end) return;
        const char *selector = p;
        while (selector < brace) {
            const char *comma = find(selector, brace, ',');
            const char *limit = trim(selector, comma);
            if (selector < limit) {
                if (sheet->count == BROWSER_CSS_RULES) { sheet->truncated = 1; return; }
                browser_css_rule *rule = &sheet->rules[sheet->count++];
                rule->selector = selector; rule->selector_end = limit;
                rule->declarations = brace + 1; rule->declarations_end = close;
            }
            selector = comma < brace ? skip(comma + 1, brace) : brace;
        }
        p = close + 1;
    }
}
static int word_in(const char *list, const char *word) {
    while (*list) {
        while (space(*list)) list++;
        const char *end = list; while (*end && !space(*end)) end++;
        if (equal(list, end, word, 1)) return 1;
        list = end;
    }
    return 0;
}
void browser_stylesheet_collect(browser_stylesheet *sheet, const browser_document *doc,
                                 browser_css_loader load, void *context) {
    sheet->count = 0; sheet->truncated = 0;
    for (int i = 1; i < doc->count; i++) {
        const browser_node *node = &doc->nodes[i];
        int embedded = browser_node_is(node, "style"), linked = browser_node_is(node, "link");
        if (!embedded && !linked) continue;
        char type[32], media[64], disabled[1];
        browser_node_attr(node, "type", type, sizeof(type));
        browser_node_attr(node, "media", media, sizeof(media));
        if (browser_node_attr(node, "disabled", disabled, sizeof(disabled))) continue;
        const char *type_end = type; while (*type_end) type_end++;
        if (type[0] && !equal(type, type_end, "text/css", 1)) continue;
        if (media[0]) {
            int applies = 0;
            const char *part = media, *media_end = media; while (*media_end) media_end++;
            while (part < media_end) {
                part = skip(part, media_end);
                const char *comma = find(part, media_end, ',');
                const char *limit = trim(part, comma);
                if (equal(part, limit, "all", 1) || equal(part, limit, "screen", 1)) applies = 1;
                part = comma < media_end ? comma + 1 : media_end;
            }
            if (!applies) continue;
        }
        if (embedded) {
            for (int child = node->first_child; child >= 0; child = doc->nodes[child].next)
                add_sheet(sheet, doc->nodes[child].start, doc->nodes[child].end);
        } else if (load) {
            char rel[64], href[128];
            browser_node_attr(node, "rel", rel, sizeof(rel));
            browser_node_attr(node, "href", href, sizeof(href));
            if (!word_in(rel, "stylesheet") || word_in(rel, "alternate") || !href[0]) continue;
            const char *data; unsigned length;
            if (load(context, href, &data, &length)) add_sheet(sheet, data, data + length);
        }
    }
}

typedef struct { const char *p, *end; int direct; } component;
static int attribute_word(const browser_node *n, const char *attr, const char *p, const char *end, int word) {
    char value[256];
    if (!browser_node_attr(n, attr, value, sizeof(value))) return 0;
    const char *v = value;
    do {
        if (word) while (space(*v)) v++;
        const char *stop = v;
        while (*stop && (!word || !space(*stop))) stop++;
        const char *a = v, *b = p;
        while (a < stop && b < end && *a == *b) { a++; b++; }
        if (a == stop && b == end) return 1;
        if (!word) break;
        v = stop;
    } while (*v);
    return 0;
}
static int compound(const browser_node *node, const char *p, const char *end, unsigned *specificity) {
    unsigned score = 0;
    if (p < end && *p == '*') p++;
    else if (p < end && ident(*p)) {
        const char *name = p;
        while (p < end && ident(*p)) p++;
        if (node && !equal(name, p, node->tag, 1)) return 0;
        score++;
    }
    while (p < end) {
        char kind = *p++;
        if (kind != '.' && kind != '#') return 0; /* Unsupported selectors never broaden matches. */
        const char *name = p;
        while (p < end && ident(*p)) p++;
        if (name == p) return 0;
        if (node && !attribute_word(node, kind == '#' ? "id" : "class", name, p, kind == '.')) return 0;
        score += kind == '#' ? 65536u : 256u;
    }
    *specificity += score;
    return 1;
}
static int match_components(const browser_document *doc, int node, component *parts, int part, unsigned *budget) {
    if (!*budget) return 0;
    --*budget;
    unsigned unused = 0;
    if (node <= 0 || !compound(&doc->nodes[node], parts[part].p, parts[part].end, &unused)) return 0;
    if (!part) return 1;
    int parent = doc->nodes[node].parent;
    if (parts[part].direct) return match_components(doc, parent, parts, part - 1, budget);
    while (parent > 0) {
        if (match_components(doc, parent, parts, part - 1, budget)) return 1;
        parent = doc->nodes[parent].parent;
    }
    return 0;
}
static int matches(const browser_document *doc, int node, const browser_css_rule *rule, unsigned *score) {
    component parts[16]; int count = 0, direct = 0;
    const char *p = rule->selector, *end = rule->selector_end;
    *score = 0;
    while ((p = skip(p, end)) < end) {
        if (count == 16 || *p == '>') return 0;
        component *part = &parts[count++];
        part->p = p; part->direct = direct;
        while (p < end && !space(*p) && *p != '>') p++;
        part->end = p;
        if (!compound(0, part->p, part->end, score)) return 0;
        p = skip(p, end); direct = 0;
        if (p < end && *p == '>') { direct = 1; p = skip(p + 1, end); if (p == end) return 0; }
    }
    unsigned budget = 4096;
    return count && match_components(doc, node, parts, count - 1, &budget);
}
static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = lower(c); return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
static int color(const char *p, const char *end, unsigned *out) {
    if (p < end && *p == '#') {
        unsigned n = (unsigned)(end - ++p), value = 0;
        if (n != 3 && n != 6) return 0;
        for (; p < end; p++) {
            int h = hex(*p); if (h < 0) return 0;
            value = n == 3 ? (value << 8) | (unsigned)h * 17 : (value << 4) | (unsigned)h;
        }
        *out = value; return 1;
    }
    static const struct { const char *name; unsigned value; } colors[] = {
        {"black",0},{"white",0xffffff},{"red",0xff0000},{"green",0x008000},
        {"blue",0x0000ff},{"yellow",0xffff00},{"gray",0x808080},{"grey",0x808080},
        {"silver",0xc0c0c0},{"maroon",0x800000},{"navy",0x000080},{"purple",0x800080},
        {"teal",0x008080},{"aqua",0x00ffff},{"lime",0x00ff00},{"fuchsia",0xff00ff},
        {"olive",0x808000},{"orange",0xffa500}
    };
    for (unsigned i = 0; i < sizeof(colors) / sizeof(colors[0]); i++)
        if (equal(p, end, colors[i].name, 1)) { *out = colors[i].value; return 1; }
    if (end - p >= 5 && lower(p[0]) == 'r' && lower(p[1]) == 'g' && lower(p[2]) == 'b' && p[3] == '(' && end[-1] == ')') {
        unsigned value = 0; p += 4; end--;
        for (int i = 0; i < 3; i++) {
            p = skip(p, end); unsigned channel = 0, digits = 0;
            while (p < end && *p >= '0' && *p <= '9') {
                channel = channel * 10 + (unsigned)(*p++ - '0');
                if (channel > 255) return 0;
                digits++;
            }
            if (!digits) return 0;
            value = (value << 8) | channel; p = skip(p, end);
            if (i < 2) { if (p == end || *p++ != ',') return 0; }
        }
        if (p != end) return 0;
        *out = value; return 1;
    }
    return 0;
}
enum { P_COLOR, P_BG, P_BOLD, P_DISPLAY, P_PRE, P_ALIGN, P_MARGIN, P_UNDERLINE, P_BGSET };
static void declarations(browser_style *style, unsigned *ranks, unsigned rank,
                          const char *p, const char *end, const browser_style *parent) {
    while ((p = skip(p, end)) < end) {
        const char *semi = find(p, end, ';'), *colon = find(p, semi, ':');
        const char *name_end = trim(p, colon);
        if (colon == semi) { p = semi < end ? semi + 1 : end; continue; }
        const char *v = skip(colon + 1, semi), *ve = trim(v, semi);
        unsigned priority = rank;
        const char *bang = find(v, ve, '!');
        if (bang < ve) {
            const char *important = skip(bang + 1, ve);
            if (!equal(important, ve, "important", 1)) { p = semi < end ? semi + 1 : end; continue; }
            priority |= 0x80000000u; ve = trim(v, bang);
        }
        int property = -1, value = 0; unsigned rgb;
        int inherit = equal(v, ve, "inherit", 1);
        if (equal(p, name_end, "color", 1)) {
            if (inherit) { property = P_COLOR; value = (int)parent->color; }
            else if (color(v, ve, &rgb)) { property = P_COLOR; value = (int)rgb; }
        } else if (equal(p, name_end, "background-color", 1) || equal(p, name_end, "background", 1)) {
            if (inherit) { property = P_BG; value = (int)parent->background; }
            else if (equal(v, ve, "transparent", 1)) { property = P_BG; value = (int)parent->background; }
            else if (color(v, ve, &rgb)) { property = P_BG; value = (int)rgb; }
        } else if (equal(p, name_end, "font-weight", 1)) {
            if (inherit) { property = P_BOLD; value = parent->bold; }
            else if (equal(v, ve, "bold", 1) || equal(v, ve, "bolder", 1) || equal(v, ve, "700", 0) || equal(v, ve, "800", 0) || equal(v, ve, "900", 0)) { property = P_BOLD; value = 1; }
            else if (equal(v, ve, "normal", 1) || equal(v, ve, "400", 0)) property = P_BOLD;
        } else if (equal(p, name_end, "display", 1)) {
            if (equal(v, ve, "none", 1)) { property = P_DISPLAY; value = BROWSER_HIDDEN; }
            else if (equal(v, ve, "block", 1) || equal(v, ve, "list-item", 1)) { property = P_DISPLAY; value = BROWSER_BLOCK; }
            else if (equal(v, ve, "inline", 1) || equal(v, ve, "inline-block", 1)) property = P_DISPLAY;
        } else if (equal(p, name_end, "white-space", 1)) {
            if (inherit) { property = P_PRE; value = parent->pre; }
            else if (equal(v, ve, "pre", 1) || equal(v, ve, "pre-wrap", 1)) { property = P_PRE; value = 1; }
            else if (equal(v, ve, "normal", 1)) property = P_PRE;
        } else if (equal(p, name_end, "text-align", 1)) {
            if (inherit) { property = P_ALIGN; value = parent->align; }
            else if (equal(v, ve, "left", 1)) property = P_ALIGN;
            else if (equal(v, ve, "center", 1)) { property = P_ALIGN; value = BROWSER_ALIGN_CENTER; }
            else if (equal(v, ve, "right", 1)) { property = P_ALIGN; value = BROWSER_ALIGN_RIGHT; }
        } else if (equal(p, name_end, "text-decoration", 1)) {
            if (equal(v, ve, "underline", 1)) { property = P_UNDERLINE; value = 1; }
            else if (equal(v, ve, "none", 1)) property = P_UNDERLINE;
        } else if (equal(p, name_end, "margin-left", 1)) {
            const char *q = v; unsigned pixels = 0, digits = 0;
            while (q < ve && *q >= '0' && *q <= '9' && pixels < 1000) { pixels = pixels * 10 + (unsigned)(*q++ - '0'); digits++; }
            if (digits && (equal(q, ve, "px", 1) || (pixels == 0 && q == ve))) { property = P_MARGIN; value = pixels > 128 ? 128 : (int)pixels; }
        }
        if (property >= 0 && priority >= ranks[property]) {
            ranks[property] = priority;
            switch (property) {
                case P_COLOR: style->color = (unsigned)value; break;
                case P_BG: style->background = (unsigned)value; style->background_set = 1; break;
                case P_BOLD: style->bold = value; break;
                case P_DISPLAY: style->display = value; break;
                case P_PRE: style->pre = value; break;
                case P_ALIGN: style->align = value; break;
                case P_MARGIN: style->margin_left = value; break;
                case P_UNDERLINE: style->underline = value; break;
            }
        }
        p = semi < end ? semi + 1 : end;
    }
}
void browser_style_compute(const browser_document *doc, int index,
                           const browser_stylesheet *sheet,
                           const browser_style *parent, browser_style *style) {
    const browser_node *node = &doc->nodes[index];
    *style = *parent;
    style->display = BROWSER_INLINE; style->margin_left = 0; style->background_set = 0;
    if (!node->tag[0]) return;
    const char *blocks[] = {"html","body","main","article","section","aside","header","footer","nav","p","div","h1","h2","h3","h4","h5","h6","ul","ol","li","dl","dt","dd","blockquote","pre","form","fieldset","table","tr","hr"};
    const char *hidden[] = {"head","style","script","title","meta","link","base","template"};
    for (unsigned i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++) if (browser_node_is(node, blocks[i])) style->display = BROWSER_BLOCK;
    for (unsigned i = 0; i < sizeof(hidden) / sizeof(hidden[0]); i++) if (browser_node_is(node, hidden[i])) style->display = BROWSER_HIDDEN;
    if (browser_node_is(node, "b") || browser_node_is(node, "strong") || browser_node_is(node, "th") ||
        (node->tag[0] == 'h' && node->tag[1] >= '1' && node->tag[1] <= '6' && !node->tag[2])) style->bold = 1;
    if (browser_node_is(node, "a")) { char href[2]; if (browser_node_attr(node, "href", href, sizeof(href))) { style->color = 0xff; style->underline = 1; } }
    if (browser_node_is(node, "u")) style->underline = 1;
    if (browser_node_is(node, "pre") || browser_node_is(node, "textarea")) style->pre = 1;
    unsigned ranks[BROWSER_CSS_PROPERTIES] = {0};
    for (int i = 0; i < sheet->count; i++) {
        unsigned specificity;
        if (matches(doc, index, &sheet->rules[i], &specificity))
            declarations(style, ranks, specificity + 1, sheet->rules[i].declarations, sheet->rules[i].declarations_end, parent);
    }
    char inline_style[768];
    if (browser_node_attr(node, "style", inline_style, sizeof(inline_style))) {
        const char *end = inline_style; while (*end) end++;
        declarations(style, ranks, 0x40000000u, inline_style, end, parent);
    }
    char hidden_attr[1];
    if (browser_node_attr(node, "hidden", hidden_attr, sizeof(hidden_attr))) style->display = BROWSER_HIDDEN;
}

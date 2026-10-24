#include "style.h"
#include "../parser/html.h"
#include "../parser/css_grid.h"

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
enum {
    P_COLOR, P_BG, P_BOLD, P_DISPLAY, P_PRE, P_ALIGN, P_UNDERLINE,
    P_MARGIN_TOP, P_MARGIN_RIGHT, P_MARGIN_BOTTOM, P_MARGIN_LEFT,
    P_PADDING_TOP, P_PADDING_RIGHT, P_PADDING_BOTTOM, P_PADDING_LEFT,
    P_WIDTH, P_MAX_WIDTH, P_HEIGHT, P_MIN_HEIGHT,
    P_BORDER_WIDTH, P_BORDER_COLOR, P_BORDER_STYLE, P_BOX_SIZING, P_LINE_HEIGHT,
    P_FLEX_DIRECTION, P_FLEX_WRAP, P_JUSTIFY, P_ALIGN_ITEMS, P_ROW_GAP, P_COLUMN_GAP,
    P_FLEX_GROW, P_FLEX_SHRINK, P_FLEX_BASIS, P_ALIGN_SELF,
    P_GRID_TRACKS, P_GRID_AUTO_ROWS, P_GRID_COLUMN, P_GRID_SPAN, P_JUSTIFY_ITEMS, P_JUSTIFY_SELF
};
static void apply_property(browser_style *s, unsigned *ranks, unsigned priority, int property, int value) {
    if (priority < ranks[property]) return;
    ranks[property] = priority;
    if (property >= P_MARGIN_TOP && property <= P_MARGIN_LEFT) s->margin[property - P_MARGIN_TOP] = value;
    else if (property >= P_PADDING_TOP && property <= P_PADDING_LEFT) s->padding[property - P_PADDING_TOP] = value;
    else switch (property) {
        case P_COLOR: s->color = (unsigned)value; break;
        case P_BG: s->background = (unsigned)value; s->background_set = 1; break;
        case P_BOLD: s->bold = value; break;
        case P_DISPLAY: s->display = value; break;
        case P_PRE: s->pre = value; break;
        case P_ALIGN: s->align = value; break;
        case P_UNDERLINE: s->underline = value; break;
        case P_WIDTH: s->width = value; break;
        case P_MAX_WIDTH: s->max_width = value; break;
        case P_HEIGHT: s->height = value; break;
        case P_MIN_HEIGHT: s->min_height = value; break;
        case P_BORDER_WIDTH: s->border_width = value; break;
        case P_BORDER_COLOR: s->border_color = (unsigned)value; break;
        case P_BORDER_STYLE: s->border_solid = value; break;
        case P_BOX_SIZING: s->border_box = value; break;
        case P_LINE_HEIGHT: s->line_height = value; break;
        case P_FLEX_DIRECTION: s->flex_column = value; break;
        case P_FLEX_WRAP: s->flex_wrap = value; break;
        case P_JUSTIFY: s->justify = value; break;
        case P_ALIGN_ITEMS: s->align_items = value; break;
        case P_ALIGN_SELF: s->align_self = value; break;
        case P_ROW_GAP: s->row_gap = value; break;
        case P_COLUMN_GAP: s->column_gap = value; break;
        case P_FLEX_GROW: s->flex_grow = value; break;
        case P_FLEX_SHRINK: s->flex_shrink = value; break;
        case P_FLEX_BASIS: s->flex_basis = value; break;
        case P_GRID_AUTO_ROWS: s->grid_auto_rows = value; break;
        case P_GRID_COLUMN: s->grid_column = value; break;
        case P_GRID_SPAN: s->grid_span = value; break;
        case P_JUSTIFY_ITEMS: s->justify_items = value; break;
        case P_JUSTIFY_SELF: s->justify_self = value; break;
    }
}
/* Length encoding: -1=auto; -(percentage+2)=percentage; nonnegative=px. */
static int length_value(const char *v, const char *end, int auto_ok, int percent_ok, int *value) {
    if (auto_ok && equal(v, end, "auto", 1)) { *value = -1; return 1; }
    const char *p = v; int number = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        number = number * 10 + *p++ - '0';
        if (number > 4096) return 0;
    }
    if (p == v) return 0;
    if (percent_ok && equal(p, end, "%", 0)) {
        if (number > 100) return 0;
        *value = -number - 2; return 1;
    }
    if (!equal(p, end, "px", 1) && !(number == 0 && p == end)) return 0;
    *value = number; return 1;
}
static int border_width_value(const char *v, const char *end, int *value) {
    if (equal(v, end, "thin", 1)) { *value = 1; return 1; }
    if (equal(v, end, "medium", 1)) { *value = 3; return 1; }
    if (equal(v, end, "thick", 1)) { *value = 5; return 1; }
    return length_value(v, end, 0, 0, value);
}
static const char *value_end(const char *p, const char *end) {
    unsigned parens = 0;
    while (p < end) {
        if (*p == '(') parens++;
        else if (*p == ')' && parens) parens--;
        else if (space(*p) && !parens) break;
        p++;
    }
    return p;
}
static int flex_number(const char *v, const char *end, int *out) {
    int number = 0;
    if (v == end) return 0;
    while (v < end && *v >= '0' && *v <= '9') {
        number = number * 10 + *v++ - '0';
        if (number > 1000) return 0;
    }
    if (v != end) return 0;
    *out = number; return 1;
}
static int flex_alignment(const char *v, const char *end, int stretch) {
    if (equal(v, end, "start", 1) || equal(v, end, "flex-start", 1)) return BROWSER_CROSS_START;
    if (equal(v, end, "end", 1) || equal(v, end, "flex-end", 1)) return BROWSER_CROSS_END;
    if (equal(v, end, "center", 1)) return BROWSER_CROSS_CENTER;
    if (stretch && equal(v, end, "stretch", 1)) return BROWSER_CROSS_STRETCH;
    return -2;
}
static int grid_integer(const char *v, const char *end, int *out) {
    if (!flex_number(v, end, out)) return 0;
    return *out >= 1 && *out <= BROWSER_GRID_TRACKS;
}
static int grid_span(const char *v, const char *end, int *out) {
    const char *stop = value_end(v, end);
    if (!equal(v, stop, "span", 1)) return 0;
    return grid_integer(skip(stop, end), end, out);
}
static void grid_declaration(browser_style *s, unsigned *ranks, unsigned priority,
                              const char *name, const char *name_end, const char *v, const char *end) {
    int value;
    if (equal(name, name_end, "grid-template-columns", 1)) {
        browser_grid_track parsed[BROWSER_GRID_TRACKS]; int count;
        if (priority < ranks[P_GRID_TRACKS] || !browser_css_grid_tracks(v, end, parsed, &count)) return;
        ranks[P_GRID_TRACKS] = priority; s->grid_track_count = count;
        for (int i = 0; i < count; i++) s->grid_tracks[i] = parsed[i];
    } else if (equal(name, name_end, "grid-auto-rows", 1)) {
        if (equal(v, end, "auto", 1)) value = 0;
        else if (!length_value(v, end, 0, 0, &value)) return;
        apply_property(s, ranks, priority, P_GRID_AUTO_ROWS, value);
    } else if (equal(name, name_end, "justify-items", 1) || equal(name, name_end, "justify-self", 1)) {
        int self = equal(name, name_end, "justify-self", 1);
        value = flex_alignment(v, end, 1);
        if (self && equal(v, end, "auto", 1)) value = -1;
        if (value >= -1) apply_property(s, ranks, priority, self ? P_JUSTIFY_SELF : P_JUSTIFY_ITEMS, value);
    } else if (equal(name, name_end, "grid-column-start", 1)) {
        if (equal(v, end, "auto", 1)) value = 0;
        else if (!grid_integer(v, end, &value)) return;
        apply_property(s, ranks, priority, P_GRID_COLUMN, value);
    } else if (equal(name, name_end, "grid-column-end", 1)) {
        if (grid_span(v, end, &value)) apply_property(s, ranks, priority, P_GRID_SPAN, value);
    } else if (equal(name, name_end, "grid-column", 1)) {
        int column = 0, span = 1;
        const char *slash = find(v, end, '/'), *first_end = trim(v, slash);
        if (equal(v, first_end, "auto", 1)) column = 0;
        else if (grid_span(v, first_end, &span)) { if (slash != end) return; }
        else if (!grid_integer(v, first_end, &column)) return;
        if (slash < end) {
            const char *next = skip(slash + 1, end);
            if (equal(next, end, "auto", 1)) span = 1;
            else if (!grid_span(next, end, &span)) {
                int last;
                if (!column || !flex_number(next, end, &last) || last <= column || last > BROWSER_GRID_TRACKS + 1) return;
                span = last - column;
            }
        }
        apply_property(s, ranks, priority, P_GRID_COLUMN, column);
        apply_property(s, ranks, priority, P_GRID_SPAN, span);
    }
}

static void flex_declaration(browser_style *s, unsigned *ranks, unsigned priority,
                              const char *name, const char *name_end, const char *v, const char *end) {
    int value;
    if (equal(name, name_end, "flex-direction", 1)) {
        if (equal(v, end, "row", 1) || equal(v, end, "column", 1))
            apply_property(s, ranks, priority, P_FLEX_DIRECTION, equal(v, end, "column", 1));
    } else if (equal(name, name_end, "flex-wrap", 1)) {
        if (equal(v, end, "wrap", 1) || equal(v, end, "nowrap", 1))
            apply_property(s, ranks, priority, P_FLEX_WRAP, equal(v, end, "wrap", 1));
    } else if (equal(name, name_end, "justify-content", 1)) {
        value = flex_alignment(v, end, 0);
        if (equal(v, end, "space-between", 1)) value = BROWSER_JUSTIFY_BETWEEN;
        else if (equal(v, end, "space-around", 1)) value = BROWSER_JUSTIFY_AROUND;
        else if (equal(v, end, "space-evenly", 1)) value = BROWSER_JUSTIFY_EVENLY;
        if (value >= 0) apply_property(s, ranks, priority, P_JUSTIFY, value);
    } else if (equal(name, name_end, "align-items", 1) || equal(name, name_end, "align-self", 1)) {
        int self = equal(name, name_end, "align-self", 1);
        value = flex_alignment(v, end, 1);
        if (self && equal(v, end, "auto", 1)) value = -1;
        if (value >= -1) apply_property(s, ranks, priority, self ? P_ALIGN_SELF : P_ALIGN_ITEMS, value);
    } else if (equal(name, name_end, "gap", 1)) {
        const char *stop = value_end(v, end), *next = skip(stop, end);
        int row, col;
        if (!length_value(v, stop, 0, 0, &row)) return;
        if (next == end) col = row;
        else if (!length_value(next, end, 0, 0, &col)) return;
        apply_property(s, ranks, priority, P_ROW_GAP, row);
        apply_property(s, ranks, priority, P_COLUMN_GAP, col);
    } else if (equal(name, name_end, "row-gap", 1) || equal(name, name_end, "column-gap", 1)) {
        if (length_value(v, end, 0, 0, &value))
            apply_property(s, ranks, priority, equal(name, name_end, "row-gap", 1) ? P_ROW_GAP : P_COLUMN_GAP, value);
    } else if (equal(name, name_end, "flex-grow", 1) || equal(name, name_end, "flex-shrink", 1)) {
        if (flex_number(v, end, &value))
            apply_property(s, ranks, priority, equal(name, name_end, "flex-grow", 1) ? P_FLEX_GROW : P_FLEX_SHRINK, value);
    } else if (equal(name, name_end, "flex-basis", 1)) {
        if (length_value(v, end, 1, 1, &value)) apply_property(s, ranks, priority, P_FLEX_BASIS, value);
    } else if (equal(name, name_end, "flex", 1)) {
        int grow = 0, shrink = 1, basis = -1;
        if (equal(v, end, "none", 1)) shrink = 0;
        else if (equal(v, end, "auto", 1)) grow = 1;
        else if (!equal(v, end, "initial", 1)) {
            const char *stop = value_end(v, end), *next = skip(stop, end);
            if (!flex_number(v, stop, &grow)) return;
            basis = 0;
            if (next < end) {
                stop = value_end(next, end);
                if (flex_number(next, stop, &shrink)) {
                    next = skip(stop, end);
                    if (next < end && !length_value(next, end, 1, 1, &basis)) return;
                } else if (!length_value(next, end, 1, 1, &basis)) return;
            }
        }
        apply_property(s, ranks, priority, P_FLEX_GROW, grow);
        apply_property(s, ranks, priority, P_FLEX_SHRINK, shrink);
        apply_property(s, ranks, priority, P_FLEX_BASIS, basis);
    } else grid_declaration(s, ranks, priority, name, name_end, v, end);
}

static void box_declaration(browser_style *s, const browser_style *parent, unsigned *ranks, unsigned priority,
                             const char *name, const char *name_end, const char *v, const char *end) {
    static const char *sides[] = {"top", "right", "bottom", "left"};
    int margin = equal(name, name_end, "margin", 1), padding = equal(name, name_end, "padding", 1);
    if (margin || padding) {
        int values[4], count = 0;
        const char *p = v;
        while ((p = skip(p, end)) < end) {
            const char *stop = value_end(p, end);
            if (count == 4 || !length_value(p, stop, margin, 0, &values[count++])) return;
            p = stop;
        }
        if (!count) return;
        int expanded[] = {values[0], count > 1 ? values[1] : values[0], count > 2 ? values[2] : values[0], count > 3 ? values[3] : count > 1 ? values[1] : values[0]};
        for (int i = 0; i < 4; i++) apply_property(s, ranks, priority, (margin ? P_MARGIN_TOP : P_PADDING_TOP) + i, expanded[i]);
        return;
    }
    for (int i = 0; i < 4; i++) {
        const char *suffix = 0; int property = 0;
        if (name_end - name > 7 && equal(name, name + 7, "margin-", 1)) { suffix = name + 7; property = P_MARGIN_TOP + i; }
        else if (name_end - name > 8 && equal(name, name + 8, "padding-", 1)) { suffix = name + 8; property = P_PADDING_TOP + i; }
        if (suffix && equal(suffix, name_end, sides[i], 1)) {
            int value;
            if (length_value(v, end, property <= P_MARGIN_LEFT, 0, &value)) apply_property(s, ranks, priority, property, value);
            return;
        }
    }
    int property = -1, value = 0;
    if (equal(name, name_end, "width", 1)) property = P_WIDTH;
    else if (equal(name, name_end, "max-width", 1)) property = P_MAX_WIDTH;
    else if (equal(name, name_end, "height", 1)) property = P_HEIGHT;
    else if (equal(name, name_end, "min-height", 1)) property = P_MIN_HEIGHT;
    if (property >= 0) {
        if (property == P_MAX_WIDTH && equal(v, end, "none", 1)) value = -1;
        else if (!length_value(v, end, property == P_WIDTH || property == P_HEIGHT, property == P_WIDTH || property == P_MAX_WIDTH, &value)) return;
        apply_property(s, ranks, priority, property, value); return;
    }
    if (equal(name, name_end, "border", 1)) {
        int width = 3, solid = 0, saw_width = 0, saw_style = 0, saw_color = 0;
        unsigned rgb = BROWSER_CURRENT_COLOR;
        const char *p = v;
        while ((p = skip(p, end)) < end) {
            const char *stop = value_end(p, end); int pixels;
            if (!saw_width && border_width_value(p, stop, &pixels)) { width = pixels; saw_width = 1; }
            else if (!saw_style && (equal(p, stop, "solid", 1) || equal(p, stop, "none", 1) || equal(p, stop, "hidden", 1))) { solid = equal(p, stop, "solid", 1); saw_style = 1; }
            else if (!saw_color && equal(p, stop, "currentcolor", 1)) { rgb = BROWSER_CURRENT_COLOR; saw_color = 1; }
            else if (!saw_color && color(p, stop, &rgb)) saw_color = 1;
            else return;
            p = stop;
        }
        if (!saw_width && !saw_style && !saw_color) return;
        apply_property(s, ranks, priority, P_BORDER_WIDTH, width);
        apply_property(s, ranks, priority, P_BORDER_STYLE, solid);
        apply_property(s, ranks, priority, P_BORDER_COLOR, (int)rgb);
    } else if (equal(name, name_end, "border-width", 1)) {
        if (border_width_value(v, end, &value)) apply_property(s, ranks, priority, P_BORDER_WIDTH, value);
    } else if (equal(name, name_end, "border-color", 1)) {
        unsigned rgb;
        if (equal(v, end, "currentcolor", 1)) rgb = BROWSER_CURRENT_COLOR;
        else if (!color(v, end, &rgb)) return;
        apply_property(s, ranks, priority, P_BORDER_COLOR, (int)rgb);
    } else if (equal(name, name_end, "border-style", 1)) {
        if (equal(v, end, "solid", 1) || equal(v, end, "none", 1) || equal(v, end, "hidden", 1))
            apply_property(s, ranks, priority, P_BORDER_STYLE, equal(v, end, "solid", 1));
    } else if (equal(name, name_end, "box-sizing", 1)) {
        if (equal(v, end, "border-box", 1) || equal(v, end, "content-box", 1))
            apply_property(s, ranks, priority, P_BOX_SIZING, equal(v, end, "border-box", 1));
    } else if (equal(name, name_end, "line-height", 1)) {
        if (equal(v, end, "inherit", 1)) value = parent->line_height;
        else if (equal(v, end, "normal", 1)) value = 12;
        else if (!length_value(v, end, 0, 0, &value)) {
            /* Fixed font metrics: unitless values multiply the 12px glyph line. */
            const char *p = v; int whole = 0, fraction = 0, scale = 1;
            while (p < end && *p >= '0' && *p <= '9') { whole = whole * 10 + *p++ - '0'; if (whole > 8) return; }
            if (p == v) return;
            if (p < end && *p == '.') {
                const char *digits = ++p;
                while (p < end && *p >= '0' && *p <= '9' && scale < 1000) { fraction = fraction * 10 + *p++ - '0'; scale *= 10; }
                if (p == digits) return;
            }
            if (p != end) return;
            value = whole * 12 + fraction * 12 / scale;
        }
        if (value < 12) value = 12;
        if (value > 96) value = 96;
        apply_property(s, ranks, priority, P_LINE_HEIGHT, value);
    } else flex_declaration(s, ranks, priority, name, name_end, v, end);
}

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
            if (equal(v, ve, "flex", 1) || equal(v, ve, "inline-flex", 1)) { property = P_DISPLAY; value = BROWSER_FLEX; }
            else if (equal(v, ve, "grid", 1) || equal(v, ve, "inline-grid", 1)) { property = P_DISPLAY; value = BROWSER_GRID; }
            else if (equal(v, ve, "none", 1)) { property = P_DISPLAY; value = BROWSER_HIDDEN; }
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
            else if (equal(v, ve, "grid", 1) || equal(v, ve, "inline-grid", 1)) { property = P_DISPLAY; value = BROWSER_GRID; }
            else if (equal(v, ve, "none", 1)) property = P_UNDERLINE;
        } else box_declaration(style, parent, ranks, priority, p, name_end, v, ve);
        if (property >= 0) apply_property(style, ranks, priority, property, value);
        p = semi < end ? semi + 1 : end;
    }
}
void browser_style_compute(const browser_document *doc, int index,
                           const browser_stylesheet *sheet,
                           const browser_style *parent, browser_style *style) {
    const browser_node *node = &doc->nodes[index];
    *style = *parent;
    style->display = BROWSER_INLINE; style->background_set = 0;
    for (int i = 0; i < 4; i++) style->margin[i] = style->padding[i] = 0;
    style->width = style->max_width = style->height = -1; style->min_height = 0;
    style->border_width = 3; style->border_solid = style->border_box = 0;
    style->border_color = BROWSER_CURRENT_COLOR;
    style->flex_column = style->flex_wrap = style->justify = 0;
    style->align_items = BROWSER_CROSS_STRETCH; style->align_self = -1;
    style->row_gap = style->column_gap = style->flex_grow = 0;
    style->flex_shrink = 1; style->flex_basis = -1;
    style->grid_track_count = style->grid_auto_rows = style->grid_column = 0; style->grid_span = 1;
    style->justify_items = BROWSER_CROSS_STRETCH; style->justify_self = -1;
    for (int i = 0; i < BROWSER_GRID_TRACKS; i++) style->grid_tracks[i] = (browser_grid_track){0, 0};
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

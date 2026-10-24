#include "document.h"
#include "../parser/html.h"

static char lower(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int alpha(char c) { c = lower(c); return c >= 'a' && c <= 'z'; }
static int space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f'; }
int browser_node_is(const browser_node *node, const char *name) {
    unsigned i = 0;
    while (node->tag[i] && name[i] && node->tag[i] == name[i]) i++;
    return node->tag[i] == name[i];
}
int browser_node_attr(const browser_node *node, const char *name, char *out, unsigned cap) {
    if (!node->tag[0] || !node->start) { if (cap) out[0] = 0; return 0; }
    return browser_html_attribute(node->start, node->end, name, out, cap);
}
static int append(browser_document *doc, int parent, const char *start, const char *end) {
    if (doc->count == BROWSER_DOM_NODES) { doc->truncated = 1; return -1; }
    int index = doc->count++;
    browser_node *n = &doc->nodes[index];
    n->start = start; n->end = end; n->parent = parent;
    n->first_child = n->last_child = n->next = -1; n->tag[0] = 0;
    if (parent >= 0) {
        browser_node *p = &doc->nodes[parent];
        if (p->last_child >= 0) doc->nodes[p->last_child].next = index;
        else p->first_child = index;
        p->last_child = index;
    }
    return index;
}
static int void_tag(const browser_node *n) {
    const char *names[] = {"area","base","br","col","embed","hr","img","input","link","meta","param","source","track","wbr"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (browser_node_is(n, names[i])) return 1;
    return 0;
}
static int block_tag(const browser_node *n) {
    const char *names[] = {"address","article","aside","blockquote","div","dl","fieldset","footer","form","h1","h2","h3","h4","h5","h6","header","hr","main","nav","ol","p","pre","section","table","ul"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (browser_node_is(n, names[i])) return 1;
    return 0;
}
void browser_document_parse(browser_document *doc, const char *html, unsigned length) {
    doc->count = 0; doc->truncated = 0;
    append(doc, -1, 0, 0); /* Synthetic document root. */
    if (!html) return;
    int stack[BROWSER_DOM_DEPTH], depth = 1;
    stack[0] = 0;
    const char *p = html, *end = html + length;
    while (p < end) {
        if (*p != '<') {
            const char *start = p++;
            while (p < end && *p != '<') p++;
            if (append(doc, stack[depth - 1], start, p) < 0) break;
            continue;
        }
        if (end - p >= 4 && p[1] == '!' && p[2] == '-' && p[3] == '-') {
            p += 4;
            while (end - p >= 3 && !(p[0] == '-' && p[1] == '-' && p[2] == '>')) p++;
            p = end - p >= 3 ? p + 3 : end;
            continue;
        }
        const char *start = p + 1;
        int closing = start < end && *start == '/';
        const char *name = start + closing;
        if (name == end || !alpha(*name)) {
            if (name < end && (*name == '!' || *name == '?')) {
                const char *stop = browser_html_tag_end(name, end);
                p = stop < end ? stop + 1 : end;
            } else {
                if (append(doc, stack[depth - 1], p, p + 1) < 0) break;
                p++;
            }
            continue;
        }
        const char *stop = browser_html_tag_end(name, end);
        if (stop == end) break;
        browser_node token;
        unsigned size = 0;
        const char *q = name;
        while (q < stop && !space(*q) && *q != '/') {
            if (size + 1 < sizeof(token.tag)) token.tag[size++] = lower(*q);
            q++;
        }
        token.tag[size] = 0;
        /* A long unknown name must not alias a recognized truncated name. */
        if (q - name >= (int)sizeof(token.tag)) token.tag[0] = '?';
        p = stop + 1;
        if (closing) {
            for (int i = depth - 1; i > 0; i--) {
                if (browser_node_is(&doc->nodes[stack[i]], token.tag)) { depth = i; break; }
            }
            continue;
        }
        if (browser_node_is(&token, "body")) {
            for (int i = depth - 1; i > 0; i--)
                if (browser_node_is(&doc->nodes[stack[i]], "head")) { depth = i; break; }
        }
        /* Common implied end tags without crossing list/table boundaries. */
        if (block_tag(&token)) {
            for (int i = depth - 1; i > 0; i--) {
                if (browser_node_is(&doc->nodes[stack[i]], "p")) { depth = i; break; }
            }
        }
        const char *implied = browser_node_is(&token, "li") ? "li" :
                              browser_node_is(&token, "tr") ? "tr" :
                              (browser_node_is(&token, "td") || browser_node_is(&token, "th")) ? "cell" : 0;
        if (implied) {
            for (int i = depth - 1; i > 0; i--) {
                browser_node *ancestor = &doc->nodes[stack[i]];
                if (browser_node_is(ancestor, implied) ||
                    (implied[0] == 'c' && (browser_node_is(ancestor, "td") || browser_node_is(ancestor, "th")))) { depth = i; break; }
                if (browser_node_is(ancestor, "ul") || browser_node_is(ancestor, "ol") || browser_node_is(ancestor, "table")) break;
            }
        }
        int index = append(doc, stack[depth - 1], name, stop);
        if (index < 0) break;
        browser_node *node = &doc->nodes[index];
        for (unsigned i = 0; i <= size; i++) node->tag[i] = token.tag[i];
        int raw = browser_node_is(node, "style") || browser_node_is(node, "script") ||
                  browser_node_is(node, "title") || browser_node_is(node, "textarea");
        if (raw) {
            const char *text = p;
            while (p < end) {
                if (end - p > (int)size + 2 && p[0] == '<' && p[1] == '/') {
                    unsigned i = 0;
                    while (i < size && lower(p[i + 2]) == node->tag[i]) i++;
                    if (i == size && (p[i + 2] == '>' || space(p[i + 2]) || p[i + 2] == '/')) break;
                }
                p++;
            }
            if (p > text && append(doc, index, text, p) < 0) break;
            const char *close = browser_html_tag_end(p, end);
            p = close < end ? close + 1 : end;
        } else if (!void_tag(node)) {
            if (depth == BROWSER_DOM_DEPTH) { doc->truncated = 1; break; }
            stack[depth++] = index;
        }
    }
}

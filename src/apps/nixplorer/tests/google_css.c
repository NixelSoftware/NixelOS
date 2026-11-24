#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../engine/style/style.h"
#include "../engine/layout/box.h"
static browser_document doc;
static browser_stylesheet sheet;
static browser_style styles[BROWSER_DOM_NODES];
static int by_id(const char *id) {
    for (int i = 1; i < doc.count; i++) {
        char value[32];
        if (browser_node_attr(&doc.nodes[i], "id", value, sizeof(value)) && !strcmp(value, id)) return i;
    }
    assert(0); return 0;
}
static int grow(browser_stylesheet *s) {
    int capacity = s->capacity * 2;
    browser_css_rule *rules = malloc((unsigned)capacity * sizeof(*rules));
    if (!rules) return 0;
    memcpy(rules, s->rules, (unsigned)s->count * sizeof(*rules));
    if (s->rules != s->inline_rules) free(s->rules);
    s->rules = rules; s->capacity = capacity; return 1;
}
int main(int argc, char **argv) {
    /* Reduced examples of the homepage's gm3/og theme variables and sizing. */
    const char *html = "<style>"
        "html{--theme:#123456;--rgb:255,0,0;--gap:12px;--a:var(--b);--b:var(--a)}"
        "#bar{--gm3-sys-color-on-surface:#1f1f1f;color:var(--og-bar-color,var(--gm3-sys-color-on-surface,#fff));padding:var(--gap);min-width:160px;white-space:nowrap}"
        "#child{--theme:red;color:var(--alias,#abcdef);background:rgba(var(--rgb),.5);min-width:50%}"
        "#bar{--alias:var(--theme);--case:red;--CASE:blue}"
        "#fallback{color:var(--missing,var(--other,#00ff00));border:1px solid var(--CASE,red)}"
        "#priority{--x:red!important}div{--x:blue}#priority{color:var(--x)}"
        "#cycle{color:var(--a,#abcdef)}#self{--x:var(--x,red);color:var(--x,blue)}"
        "#initial{--theme:initial;color:var(--theme,#fedcba)}"
        "#sizing{width:20px;min-width:85px;max-width:40px;padding:10px;box-sizing:border-box}"
        "</style><html><body><div id='bar'><span id='child'>x</span>"
        "<span id='fallback'>y</span></div><div id='priority' style='--x:green'>p</div>"
        "<div id='cycle'>c</div><div id='self'>s</div><div id='initial'>i</div>"
        "<div id='sizing'>z</div></body></html>";
    browser_document_parse(&doc, html, (unsigned)strlen(html));
    assert(!doc.truncated);
    browser_stylesheet_collect(&sheet, &doc, 0, 0);
    assert(!sheet.truncated);
    styles[0] = (browser_style){.font_size=16,.root_font_size=16,.line_height=12,.viewport_width=480,.viewport_height=360,.text_alpha=255};
    for (int i = 1; i < doc.count; i++)
        browser_style_compute(&doc, i, &sheet, &styles[doc.nodes[i].parent], &styles[i]);
    browser_style *bar = &styles[by_id("bar")], *child = &styles[by_id("child")];
    assert(bar->color == 0x1f1f1f && bar->padding[0] == 12 && bar->min_width == 160 && bar->nowrap);
    assert(child->color == 0x123456 && child->background_alpha == 128 && child->background_rgb == 0xff0000);
    assert(child->nowrap && child->min_width == -52);
    assert(styles[by_id("fallback")].color == 0x00ff00 && styles[by_id("fallback")].border_color == 0x0000ff);
    assert(styles[by_id("priority")].color == 0xff0000);
    assert(styles[by_id("cycle")].color == 0xabcdef && styles[by_id("self")].color == 0x0000ff);
    assert(styles[by_id("initial")].color == 0xfedcba);
    browser_box box;
    browser_box_begin(&box, &styles[by_id("sizing")], 0, 400, 0);
    assert(box.width == 85 && box.content_width == 65);
    browser_style narrow = *child; narrow.width = 20;
    browser_box_begin(&box, &narrow, 0, 400, 0);
    assert(box.width == 200);
    if (argc == 2) {
        FILE *file = fopen(argv[1], "rb"); assert(file);
        char *page = malloc(1024u * 1024u); assert(page);
        unsigned length = (unsigned)fread(page, 1, 1024u * 1024u, file); fclose(file);
        browser_document_parse(&doc, page, length);
        browser_stylesheet_collect_growing(&sheet, &doc, 0, 0, grow);
        assert(!doc.truncated && !sheet.truncated);
        for (int i = 1; i < doc.count; i++)
            browser_style_compute(&doc, i, &sheet, &styles[doc.nodes[i].parent], &styles[i]);
        printf("Google HTML: computed styles for %d nodes using %d embedded rules\n", doc.count, sheet.count);
        if (sheet.rules != sheet.inline_rules) free(sheet.rules);
        free(page);
    }
    puts("PASS Google theme variables, fallbacks, cascade, inheritance, cycles, nowrap and minimum widths");
    return 0;
}

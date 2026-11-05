#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../engine/style/style.h"

static browser_document doc;
static browser_stylesheet sheet;
static unsigned grows, fail_after;
static int grow(browser_stylesheet *s) {
    if (grows == fail_after) return 0;
    int capacity = s->capacity * 2;
    browser_css_rule *rules = malloc((unsigned)capacity * sizeof(*rules));
    assert(rules);
    memcpy(rules, s->rules, (unsigned)s->count * sizeof(*rules));
    if (s->rules != s->inline_rules) free(s->rules);
    s->rules = rules; s->capacity = capacity; grows++;
    return 1;
}
static void release(void) {
    if (sheet.rules != sheet.inline_rules) free(sheet.rules);
}
int main(int argc, char **argv) {
    char *html = malloc(1000000);
    assert(html);
    unsigned size = (unsigned)sprintf(html, "<style>");
    for (unsigned i = 0; i < 1000; i++)
        size += (unsigned)sprintf(html + size, ".unused%u{color:red}", i);
    size += (unsigned)sprintf(html + size, "div{color:#123456}</style><div>hello</div>");
    browser_document_parse(&doc, html, size);
    assert(!doc.truncated);
    browser_stylesheet_collect(&sheet, &doc, 0, 0);
    assert(sheet.truncated && sheet.count == BROWSER_CSS_RULES);
    fail_after = 100;
    browser_stylesheet_collect_growing(&sheet, &doc, 0, 0, grow);
    assert(!sheet.truncated && sheet.count == 1001 && grows > 1);
    browser_style parent = {0}, style;
    browser_style_compute(&doc, 3, &sheet, &parent, &style);
    assert(style.color == 0x123456);
    release();
    grows = 0; fail_after = 1;
    browser_stylesheet_collect_growing(&sheet, &doc, 0, 0, grow);
    assert(sheet.truncated && sheet.count == 2 * BROWSER_CSS_RULES);
    release();
    /* Recollect safely after an allocation failure. */
    grows = 0; fail_after = 100;
    browser_stylesheet_collect_growing(&sheet, &doc, 0, 0, grow);
    assert(!sheet.truncated && sheet.count == 1001);
    release();
    if (argc == 2) {
        FILE *file = fopen(argv[1], "rb"); assert(file);
        size = (unsigned)fread(html, 1, 1000000, file); fclose(file);
        browser_document_parse(&doc, html, size);
        grows = 0;
        browser_stylesheet_collect_growing(&sheet, &doc, 0, 0, grow);
        printf("Page: %u bytes, %d nodes, %d CSS rules, DOM truncated=%d, CSS truncated=%d\n",
               size, doc.count, sheet.count, doc.truncated, sheet.truncated);
        assert(!doc.truncated && !sheet.truncated);
        release();
    }
    free(html);
    puts("PASS growing CSS rules, cascade after growth, OOM and recollection");
    return 0;
}

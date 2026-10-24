#ifndef NIXEL_BROWSER_STYLE_H
#define NIXEL_BROWSER_STYLE_H
#include "document.h"
#define BROWSER_CSS_RULES 160
#define BROWSER_CSS_PROPERTIES 9

enum { BROWSER_INLINE, BROWSER_BLOCK, BROWSER_HIDDEN };
enum { BROWSER_ALIGN_LEFT, BROWSER_ALIGN_CENTER, BROWSER_ALIGN_RIGHT };
typedef struct {
    unsigned color, background;
    int bold, display, pre, align, margin_left, underline, background_set;
} browser_style;
typedef struct {
    const char *selector, *selector_end, *declarations, *declarations_end;
} browser_css_rule;
typedef struct {
    browser_css_rule rules[BROWSER_CSS_RULES];
    int count, truncated;
} browser_stylesheet;

typedef int (*browser_css_loader)(void *context, const char *url, const char **data, unsigned *length);
void browser_stylesheet_collect(browser_stylesheet *sheet, const browser_document *doc,
                                 browser_css_loader load, void *context);
void browser_style_compute(const browser_document *doc, int node,
                           const browser_stylesheet *sheet,
                           const browser_style *parent, browser_style *style);
#endif

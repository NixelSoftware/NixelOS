#ifndef NIXEL_BROWSER_STYLE_H
#define NIXEL_BROWSER_STYLE_H
#include "../dom/document.h"
#define BROWSER_CSS_RULES 160
#define BROWSER_CSS_PROPERTIES 34
#define BROWSER_CURRENT_COLOR 0x01000000u
enum { BROWSER_TOP, BROWSER_RIGHT, BROWSER_BOTTOM, BROWSER_LEFT };

enum { BROWSER_INLINE, BROWSER_BLOCK, BROWSER_HIDDEN, BROWSER_FLEX };
enum { BROWSER_ALIGN_LEFT, BROWSER_ALIGN_CENTER, BROWSER_ALIGN_RIGHT };
enum { BROWSER_JUSTIFY_START, BROWSER_JUSTIFY_END, BROWSER_JUSTIFY_CENTER,
       BROWSER_JUSTIFY_BETWEEN, BROWSER_JUSTIFY_AROUND, BROWSER_JUSTIFY_EVENLY };
enum { BROWSER_CROSS_START, BROWSER_CROSS_END, BROWSER_CROSS_CENTER, BROWSER_CROSS_STRETCH };
typedef struct {
    unsigned color, background;
    int bold, display, pre, align, underline, background_set;
    int margin[4], padding[4];
    /* Widths: -1=auto, -(percentage+2)=percentage, otherwise pixels. */
    int width, max_width, height, min_height;
    int border_width, border_solid, border_box, line_height;
    unsigned border_color;
    int flex_column, flex_wrap, justify, align_items, align_self;
    int row_gap, column_gap, flex_grow, flex_shrink, flex_basis;
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

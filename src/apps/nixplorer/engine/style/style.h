#ifndef NIXEL_BROWSER_STYLE_H
#define NIXEL_BROWSER_STYLE_H
#include "../dom/document.h"
#include "../parser/css_color.h"
#define BROWSER_CSS_RULES 160
#define BROWSER_CSS_PROPERTIES 44
#define BROWSER_GRID_TRACKS 8
#define BROWSER_CURRENT_COLOR BROWSER_CSS_CURRENT_COLOR
enum { BROWSER_TOP, BROWSER_RIGHT, BROWSER_BOTTOM, BROWSER_LEFT };

enum { BROWSER_INLINE, BROWSER_BLOCK, BROWSER_HIDDEN, BROWSER_FLEX, BROWSER_GRID };
enum { BROWSER_ALIGN_LEFT, BROWSER_ALIGN_CENTER, BROWSER_ALIGN_RIGHT };
enum { BROWSER_JUSTIFY_START, BROWSER_JUSTIFY_END, BROWSER_JUSTIFY_CENTER,
       BROWSER_JUSTIFY_BETWEEN, BROWSER_JUSTIFY_AROUND, BROWSER_JUSTIFY_EVENLY };
enum { BROWSER_CROSS_START, BROWSER_CROSS_END, BROWSER_CROSS_CENTER, BROWSER_CROSS_STRETCH };
enum { BROWSER_TRACK_PX, BROWSER_TRACK_FR, BROWSER_TRACK_AUTO, BROWSER_TRACK_PERCENT };
typedef struct { int kind, value; } browser_grid_track;
typedef struct {
    unsigned color, background, painted_background; /* Resolved against the containing backdrop. */
    unsigned text_rgb, text_alpha, background_rgb, background_alpha, border_alpha;
    int text_transform, visibility_hidden;
    int font_size, root_font_size, viewport_width, viewport_height, line_height_value;
    int nowrap;
    int bold, display, pre, align, underline, background_set; /* decoration bits: underline=1, strike=2, overline=4 */
    int margin[4], padding[4];
    /* Widths: -1=auto, -(percentage+2)=percentage, otherwise pixels. */
    int width, min_width, max_width, height, min_height;
    int border_width, border_solid, border_box, line_height;
    unsigned border_color;
    int flex_column, flex_wrap, justify, align_items, align_self;
    int row_gap, column_gap, flex_grow, flex_shrink, flex_basis;
    browser_grid_track grid_tracks[BROWSER_GRID_TRACKS];
    int grid_track_count, grid_auto_rows, grid_column, grid_span;
    int justify_items, justify_self;
} browser_style;
typedef struct {
    const char *selector, *selector_end, *declarations, *declarations_end;
} browser_css_rule;
typedef struct {
    browser_css_rule inline_rules[BROWSER_CSS_RULES];
    browser_css_rule *rules;
    int count, capacity, truncated;
} browser_stylesheet;

typedef int (*browser_css_loader)(void *context, const char *url, const char **data, unsigned *length);
/* grow preserves existing rules and increases capacity, returning 0 on OOM.
 * The caller releases any external rule storage before collecting again. */
typedef int (*browser_css_grow)(browser_stylesheet *sheet);
void browser_stylesheet_collect_growing(browser_stylesheet *sheet, const browser_document *doc,
                                 browser_css_loader load, void *context, browser_css_grow grow);
void browser_stylesheet_collect(browser_stylesheet *sheet, const browser_document *doc,
                                 browser_css_loader load, void *context);
void browser_style_compute(const browser_document *doc, int node,
                           const browser_stylesheet *sheet,
                           const browser_style *parent, browser_style *style);
#endif

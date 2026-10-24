#ifndef NIXEL_BROWSER_FLEX_H
#define NIXEL_BROWSER_FLEX_H
#include "../style/style.h"
#define BROWSER_FLEX_ITEMS 48
/* Bounded single-axis sizing. Geometry is in pixels; text intrinsic widths
 * come from the native fixed-font layout. Grid and baseline alignment are
 * not part of this adapter. */
typedef struct {
    int node, basis, minimum, maximum, grow, shrink;
    int x, width, row;
    int first_line, last_line, first_box, last_box, height;
} browser_flex_item;
int browser_flex_plan(browser_flex_item *items, int count, int available,
                       int gap, int wrap, int justify);
#endif

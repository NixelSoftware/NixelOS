#ifndef NIXPLORER_GRID_H
#define NIXPLORER_GRID_H
#include "flex.h"
#define BROWSER_GRID_ITEMS BROWSER_FLEX_ITEMS
/* Row-major placement with column starts/spans and auto-height rows.
 * Explicit rows, row spans, named areas, and implicit columns are unsupported.
 * Tracks fit the viewport, retaining at least one native glyph per column. */
typedef struct {
    browser_flex_item layout;
    int column, span, preferred;
} browser_grid_item;
int browser_grid_plan(const browser_style *style, int available,
                       browser_grid_item *items, int count);
#endif

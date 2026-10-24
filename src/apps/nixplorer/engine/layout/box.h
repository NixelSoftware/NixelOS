#ifndef NIXEL_BROWSER_BOX_H
#define NIXEL_BROWSER_BOX_H
#include "../style/style.h"
/* Native normal-flow geometry; reference: nh-css/Layout/BoxModel.c.
 * Fixed-font content remains at least one glyph wide where space permits.
 * Oversized boxes fit the containing block; overflowing height expands. */
typedef struct {
    int x, y, width, height;
    int content_x, content_y, content_width;
    int padding_top, padding_bottom, border, margin_bottom;
    unsigned background, border_color;
    int paint_background;
} browser_box;
void browser_box_begin(browser_box *box, const browser_style *style,
                         int container_x, int container_width, int top);
int browser_box_finish(browser_box *box, const browser_style *style, int content_bottom);
#endif

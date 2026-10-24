#include "box.h"

static int clamp(int value, int max) {
    if (value < 0) return 0;
    return value > max ? max : value;
}
static int width_value(int value, int containing) {
    return value < -1 ? containing * (-value - 2) / 100 : value;
}
void browser_box_begin(browser_box *b, const browser_style *s,
                         int x, int width, int top) {
    if (width < 0) width = 0;
    int minimum = width < 8 ? width : 8;
    int ml = clamp(s->margin[BROWSER_LEFT], width - minimum);
    int mr = clamp(s->margin[BROWSER_RIGHT], width - minimum - ml);
    int available = width - ml - mr;
    b->border = s->border_solid ? clamp(s->border_width, (available - minimum) / 2) : 0;
    int pl = clamp(s->padding[BROWSER_LEFT], available - minimum - 2 * b->border);
    int pr = clamp(s->padding[BROWSER_RIGHT], available - minimum - 2 * b->border - pl);
    int edges = pl + pr + 2 * b->border;
    int content = available - edges;
    int specified = width_value(s->width, width);
    if (specified >= 0) content = s->border_box ? specified - edges : specified;
    int maximum = width_value(s->max_width, width);
    if (maximum >= 0) {
        if (s->border_box) maximum -= edges;
        if (content > maximum) content = maximum;
    }
    if (content < minimum) content = minimum;
    if (content > available - edges) content = available - edges;
    b->width = content + edges;
    int remainder = available - b->width;
    if (s->margin[BROWSER_LEFT] == -1) ml += s->margin[BROWSER_RIGHT] == -1 ? remainder / 2 : remainder;
    b->x = x + ml; b->y = top + clamp(s->margin[BROWSER_TOP], 4096);
    b->content_x = b->x + b->border + pl; b->content_width = content;
    b->padding_top = clamp(s->padding[BROWSER_TOP], 4096);
    b->padding_bottom = clamp(s->padding[BROWSER_BOTTOM], 4096);
    b->content_y = b->y + b->border + b->padding_top;
    b->margin_bottom = clamp(s->margin[BROWSER_BOTTOM], 4096);
    b->height = 0; b->background = s->background;
    b->paint_background = s->background_set;
    b->border_color = s->border_color == BROWSER_CURRENT_COLOR ? s->color : s->border_color;
}
int browser_box_finish(browser_box *b, const browser_style *s, int content_bottom) {
    int content = content_bottom - b->content_y;
    int edges = 2 * b->border + b->padding_top + b->padding_bottom;
    int minimum = s->min_height;
    if (s->height > minimum) minimum = s->height;
    if (s->border_box) minimum -= edges;
    if (content < minimum) content = minimum;
    if (content < 0) content = 0;
    b->height = content + edges;
    return b->y + b->height + b->margin_bottom;
}

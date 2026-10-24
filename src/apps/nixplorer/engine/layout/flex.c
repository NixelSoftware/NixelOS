#include "flex.h"

/* Avoid hosted 64-bit division helpers in the freestanding i386 binary. */
static int share(int amount, int weight, int total) {
    if (total <= 0 || weight <= 0) return 0;
    while ((unsigned)amount > 0x7fffffffu / (unsigned)weight) {
        weight = (weight + 1) / 2; total = (total + 1) / 2;
    }
    return amount * weight / total;
}
static void size_row(browser_flex_item *items, int start, int end, int available, int gap, int justify) {
    int used = (end - start - 1) * gap;
    for (int i = start; i < end; i++) used += items[i].width;
    /* Iterative freezing redistributes free space when an item hits its limit. */
    for (int pass = 0; pass < end - start && used != available; pass++) {
        int grow = used < available, total = 0, changed = 0;
        for (int i = start; i < end; i++) {
            if ((grow && items[i].width < items[i].maximum) || (!grow && items[i].width > items[i].minimum))
                total += grow ? items[i].grow : items[i].shrink * items[i].basis;
        }
        if (!total) break;
        int remaining = grow ? available - used : used - available;
        for (int i = start; i < end; i++) {
            int limit = grow ? items[i].maximum - items[i].width : items[i].width - items[i].minimum;
            if (limit <= 0) continue;
            int weight = grow ? items[i].grow : items[i].shrink * items[i].basis;
            if (!weight) continue;
            int pixels = share(remaining, weight, total);
            if (pixels > limit) pixels = limit;
            items[i].width += grow ? pixels : -pixels;
            used += grow ? pixels : -pixels;
            remaining -= pixels; total -= weight; changed += pixels;
        }
        if (!changed) break;
    }
    int extra = available - used;
    if (extra < 0) extra = 0;
    int count = end - start, offset = 0;
    if (justify == BROWSER_JUSTIFY_CENTER) offset = extra / 2;
    else if (justify == BROWSER_JUSTIFY_END) offset = extra;
    for (int i = start; i < end; i++) {
        int local = i - start, spacing = 0;
        if (justify == BROWSER_JUSTIFY_BETWEEN && count > 1) spacing = extra * local / (count - 1);
        else if (justify == BROWSER_JUSTIFY_AROUND) spacing = extra * (2 * local + 1) / (2 * count);
        else if (justify == BROWSER_JUSTIFY_EVENLY) spacing = extra * (local + 1) / (count + 1);
        items[i].x = offset + spacing;
        offset += items[i].width + gap;
    }
}
int browser_flex_plan(browser_flex_item *items, int count, int available, int gap, int wrap, int justify) {
    if (count <= 0 || count > BROWSER_FLEX_ITEMS || available < 8) return 0;
    if (gap < 0) gap = 0;
    if (gap > available) gap = available;
    int minimum_total = (count - 1) * gap;
    for (int i = 0; i < count; i++) {
        int minimum = items[i].minimum < 8 ? 8 : items[i].minimum;
        if (!items[i].shrink && items[i].basis > minimum) minimum = items[i].basis;
        if (minimum > available) minimum = available;
        minimum_total += minimum;
    }
    /* No horizontal scrolling: wrap rather than painting past fixed-font minima. */
    if (minimum_total > available) wrap = 1;
    int start = 0, used = 0, row = 0;
    for (int i = 0; i < count; i++) {
        if (items[i].minimum < 8) items[i].minimum = 8;
        if (items[i].minimum > available) items[i].minimum = available;
        if (items[i].maximum > available) items[i].maximum = available;
        if (items[i].maximum < items[i].minimum) items[i].maximum = items[i].minimum;
        int width = items[i].basis;
        if (width < items[i].minimum) width = items[i].minimum;
        if (width > items[i].maximum) width = items[i].maximum;
        if (wrap && i > start && used + gap + width > available) {
            size_row(items, start, i, available, gap, justify);
            row++; start = i; used = 0;
        }
        if (i > start) used += gap;
        items[i].width = width; items[i].row = row; used += width;
    }
    size_row(items, start, count, available, gap, justify);
    return row + 1;
}

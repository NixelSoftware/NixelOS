#include "grid.h"

int browser_grid_plan(const browser_style *style, int available,
                       browser_grid_item *items, int count) {
    if (available < 8 || count < 1 || count > BROWSER_GRID_ITEMS) return 0;
    int columns = style->grid_track_count ? style->grid_track_count : 1;
    if (columns > BROWSER_GRID_TRACKS) columns = BROWSER_GRID_TRACKS;
    if (columns > available / 8) columns = available / 8;
    int gap = style->column_gap;
    if (gap < 0) gap = 0;
    int maximum_gap = columns > 1 ? (available - columns * 8) / (columns - 1) : 0;
    if (gap > maximum_gap) gap = maximum_gap;
    int cursor = 0, row = 0;
    for (int i = 0; i < count; i++) {
        int span = items[i].span;
        if (span < 1) span = 1;
        if (span > columns) span = columns;
        int column = items[i].column > 0 ? items[i].column - 1 : cursor;
        if (items[i].column > 0 && (column >= columns || column + span > columns)) column = 0;
        if (column < cursor || column + span > columns) {
            row++; cursor = 0;
            column = items[i].column > 0 ? items[i].column - 1 : 0;
            if (column + span > columns) column = 0;
        }
        items[i].column = column; items[i].span = span; items[i].layout.row = row;
        cursor = column + span;
    }
    browser_flex_item tracks[BROWSER_GRID_TRACKS] = {{0}};
    int fr = 0;
    for (int i = 0; i < columns; i++) {
        browser_grid_track track = style->grid_track_count ? style->grid_tracks[i] : (browser_grid_track){BROWSER_TRACK_AUTO, 0};
        tracks[i].minimum = 8; tracks[i].maximum = available; tracks[i].shrink = 1;
        if (track.kind == BROWSER_TRACK_FR) { tracks[i].basis = 8; tracks[i].grow = track.value; fr += track.value; }
        else if (track.kind == BROWSER_TRACK_PX) tracks[i].basis = track.value;
        else if (track.kind == BROWSER_TRACK_PERCENT) tracks[i].basis = available * track.value / 100;
        else {
            int preferred = 8;
            for (int j = 0; j < count; j++) {
                if (i < items[j].column || i >= items[j].column + items[j].span) continue;
                int share = (items[j].preferred - (items[j].span - 1) * gap) / items[j].span;
                if (share > preferred) preferred = share;
            }
            tracks[i].basis = preferred;
        }
    }
    for (int i = 0; i < columns; i++) {
        int kind = style->grid_track_count ? style->grid_tracks[i].kind : BROWSER_TRACK_AUTO;
        if (kind == BROWSER_TRACK_AUTO && !fr) tracks[i].grow = 1;
    }
    browser_flex_plan(tracks, columns, available, gap, 0, style->justify);
    for (int i = 0; i < count; i++) {
        int first = items[i].column, last = first + items[i].span - 1;
        items[i].layout.x = tracks[first].x;
        items[i].layout.width = tracks[last].x + tracks[last].width - tracks[first].x;
    }
    return row + 1;
}

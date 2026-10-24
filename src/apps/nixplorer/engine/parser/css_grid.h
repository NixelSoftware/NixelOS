#ifndef NIXPLORER_CSS_GRID_H
#define NIXPLORER_CSS_GRID_H
#include "../style/style.h"

/* Bounded explicit track lists: px, %, fr, auto, and integer repeat(). */
int browser_css_grid_tracks(const char *start, const char *end,
                              browser_grid_track *tracks, int *count);
#endif

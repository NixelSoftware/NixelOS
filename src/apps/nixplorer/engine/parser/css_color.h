#ifndef NIXPLORER_CSS_COLOR_H
#define NIXPLORER_CSS_COLOR_H
#define BROWSER_CSS_CURRENT_COLOR 0x01000000u
typedef struct { unsigned rgb, alpha; } browser_css_color;
/* No allocations or floating point. Alpha is in [0,255]. */
int browser_css_parse_color(const char *p, const char *end, browser_css_color *out);
unsigned browser_css_blend(unsigned rgb, unsigned alpha, unsigned backdrop);
#endif

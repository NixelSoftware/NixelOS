#ifndef NIXPLORER_SELECTORS_H
#define NIXPLORER_SELECTORS_H
#include "../dom/document.h"
/* Bounded selector matching; unsupported syntax fails closed. */
int browser_css_matches(const browser_document *doc, int node,
                           const char *start, const char *end, unsigned *specificity);
#endif

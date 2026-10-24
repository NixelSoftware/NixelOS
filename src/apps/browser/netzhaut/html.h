#ifndef NIXEL_BROWSER_HTML_H
#define NIXEL_BROWSER_HTML_H

/* All input spans are bounded; no allocation or hosted C runtime required. */
const char *browser_html_tag_end(const char *tag, const char *end);
int browser_html_attribute(const char *tag, const char *end,
                           const char *name, char *out, unsigned capacity);
/* Returns bytes consumed, including '&', or zero for a literal ampersand.
 * attribute enables HTML's ambiguous-ampersand rule. */
unsigned browser_html_reference(const char *text, const char *end,
                                int attribute, unsigned *codepoint);
char browser_html_display_char(unsigned codepoint);
#endif

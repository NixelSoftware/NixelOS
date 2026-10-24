#ifndef NIXEL_BROWSER_HTML_H
#define NIXEL_BROWSER_HTML_H

const char *browser_html_tag_end(const char *tag, const char *end);
int browser_html_attribute(const char *tag, const char *end,
                           const char *name, char *out, unsigned capacity);
unsigned browser_html_reference(const char *text, const char *end,
                                int attribute, unsigned *codepoint);
#endif

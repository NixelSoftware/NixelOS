#ifndef NIXEL_BROWSER_DOCUMENT_H
#define NIXEL_BROWSER_DOCUMENT_H

#define BROWSER_DOM_NODES 768
#define BROWSER_DOM_DEPTH 48
/* Source spans borrow the HTTP document buffer until the next navigation. */
typedef struct {
    const char *start, *end;
    int parent, first_child, last_child, next;
    char tag[20]; /* Empty name denotes a text node. */
} browser_node;
typedef struct {
    browser_node nodes[BROWSER_DOM_NODES];
    int count, truncated;
} browser_document;

void browser_document_parse(browser_document *doc, const char *html, unsigned length);
int browser_node_is(const browser_node *node, const char *name);
int browser_node_attr(const browser_node *node, const char *name, char *out, unsigned cap);
#endif

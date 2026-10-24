#ifndef NIXEL_BROWSER_HTTP_H
#define NIXEL_BROWSER_HTTP_H
int browser_http_body(unsigned char *response, unsigned length,
                      unsigned char **body, unsigned *body_length);
#endif

#ifndef NIXEL_BROWSER_HTTP_H
#define NIXEL_BROWSER_HTTP_H
/* Decode an HTTP response body in place. Returns zero for malformed,
 * truncated, or unsupported transfer/content encoding. */
int browser_http_body(unsigned char *response, unsigned length,
                      unsigned char **body, unsigned *body_length);
#endif

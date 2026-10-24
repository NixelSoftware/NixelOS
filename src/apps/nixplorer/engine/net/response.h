#ifndef NIXPLORER_HTTP_RESPONSE_H
#define NIXPLORER_HTTP_RESPONSE_H
#include "http.h"
#define HTTP_HEADER_MAX 8192u
#define HTTP_RAW_MAX (BROWSER_HTTP_RESPONSE_BODY_MAX + HTTP_HEADER_MAX)
/* 0=incomplete, 1=complete, negative=BROWSER_HTTP_* error. Chunked data is
 * compacted only after the entire framed response has been validated. */
int browser_http_parse_response(unsigned char *raw, unsigned length, int eof,
                          browser_http_response_t *response, unsigned *body_offset);
#endif

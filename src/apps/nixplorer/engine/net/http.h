#ifndef NIXEL_BROWSER_HTTP_H
#define NIXEL_BROWSER_HTTP_H

#define BROWSER_HTTP_HOST_MAX 254u
#define BROWSER_HTTP_PATH_MAX 512u
#define BROWSER_HTTP_ACCEPT_MAX 128u
#define BROWSER_HTTP_REQUEST_BODY_MAX 512u
/* Arithmetic ceiling; actual capacity is determined by heap allocations. */
#define BROWSER_HTTP_RESPONSE_BODY_MAX 0x3fffffffu
#define BROWSER_HTTP_CONTENT_TYPE_MAX 128u

enum {
    BROWSER_HTTP_INVALID = -1, BROWSER_HTTP_LIMIT = -2, BROWSER_HTTP_DNS = -3,
    BROWSER_HTTP_CONNECT = -4, BROWSER_HTTP_TIMEOUT = -5, BROWSER_HTTP_PROTOCOL = -6,
    BROWSER_HTTP_ENCODING = -7, BROWSER_HTTP_TLS = -8,
    BROWSER_HTTP_TLS_PLATFORM = -9, BROWSER_HTTP_HEADER_LIMIT = -10,
    BROWSER_HTTP_BODY_LIMIT = -11, BROWSER_HTTP_MEMORY = -12
};
typedef struct {
    unsigned status, body_length;
    char location[BROWSER_HTTP_PATH_MAX];
    char content_type[BROWSER_HTTP_CONTENT_TYPE_MAX];
} browser_http_response_t;

/* On success, *output owns a heap allocation; release with browser_http_free.
 * On failure, *output is null. Headers and body grow with available RAM. */
void browser_http_free(void *allocation);
int browser_http_request_alloc_scheme(unsigned secure, const char *host, const char *path, const char *accept,
                           unsigned post, const void *body, unsigned body_length,
                           void **output, unsigned *length, browser_http_response_t *response);
/* secure selects HTTPS on port 443; false selects HTTP on port 80. */
int browser_http_tls_error(void); /* detailed TLS error from the most recent request */
int browser_http_request_scheme(unsigned secure, const char *host, const char *path, const char *accept,
                           unsigned post, const void *body, unsigned body_length,
                           void *output, unsigned capacity, unsigned *length,
                           browser_http_response_t *response);
int browser_http_request(const char *host, const char *path, const char *accept,
                           unsigned post, const void *body, unsigned body_length,
                           void *output, unsigned capacity, unsigned *length,
                           browser_http_response_t *response);
#endif

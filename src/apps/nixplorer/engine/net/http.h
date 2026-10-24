#ifndef NIXEL_BROWSER_HTTP_H
#define NIXEL_BROWSER_HTTP_H

#define BROWSER_HTTP_HOST_MAX 254u
#define BROWSER_HTTP_PATH_MAX 512u
#define BROWSER_HTTP_ACCEPT_MAX 128u
#define BROWSER_HTTP_REQUEST_BODY_MAX 512u
#define BROWSER_HTTP_RESPONSE_BODY_MAX 65536u
#define BROWSER_HTTP_CONTENT_TYPE_MAX 128u

enum {
    BROWSER_HTTP_INVALID = -1, BROWSER_HTTP_LIMIT = -2, BROWSER_HTTP_DNS = -3,
    BROWSER_HTTP_CONNECT = -4, BROWSER_HTTP_TIMEOUT = -5, BROWSER_HTTP_PROTOCOL = -6,
    BROWSER_HTTP_ENCODING = -7
};
typedef struct {
    unsigned status, body_length;
    char location[BROWSER_HTTP_PATH_MAX];
    char content_type[BROWSER_HTTP_CONTENT_TYPE_MAX];
} browser_http_response_t;

int browser_http_request(const char *host, const char *path, const char *accept,
                           unsigned post, const void *body, unsigned body_length,
                           void *output, unsigned capacity, unsigned *length,
                           browser_http_response_t *response);
#endif

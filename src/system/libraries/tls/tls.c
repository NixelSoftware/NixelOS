#include "tls.h"
#include <wolfssl/ssl.h>
#include <wolfssl/error-ssl.h>
#include <wolfssl/wolfcrypt/error-crypt.h>

static nixel_tls_platform platform;
static int initialized;
struct nixel_tls_client {
    WOLFSSL_CTX *ctx;
    WOLFSSL *ssl;
    nixel_tls_io send, receive;
    void *io;
    int error;
};
void *nixel_tls_alloc(size_t n) { return platform.alloc ? platform.alloc(n) : NULL; }
void nixel_tls_free(void *p) { if (platform.free) platform.free(p); }
void *nixel_tls_realloc(void *p, size_t n) {
    return platform.realloc ? platform.realloc(p, n) : NULL;
}
int nixel_tls_seed(unsigned char *p, unsigned int n) {
    return platform.entropy && platform.entropy(p, n) == 0 ? 0 : RNG_FAILURE_E;
}
long nixel_tls_time(long *out) {
    long now = platform.unix_time ? platform.unix_time() : -1;
    if (out) *out = now;
    return now;
}
/* Gregorian calendar conversion, bounded to the range of signed 32-bit time. */
struct tm *nixel_tls_gmtime(const long *stamp, struct tm *out) {
    static const int months[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    long days, seconds;
    int year = 1970, month = 0, leap;
    if (!stamp || !out || *stamp < 0 || *stamp > 2147483647L) return NULL;
    days = *stamp / 86400; seconds = *stamp % 86400;
    out->tm_sec = seconds % 60; out->tm_min = seconds / 60 % 60;
    out->tm_hour = seconds / 3600; out->tm_wday = (days + 4) % 7;
    for (;;) {
        leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
        if (days < 365 + leap) break;
        days -= 365 + leap; year++;
    }
    out->tm_yday = days;
    while (days >= months[month] + (month == 1 && leap)) {
        days -= months[month] + (month == 1 && leap); month++;
    }
    out->tm_year = year - 1900; out->tm_mon = month;
    out->tm_mday = days + 1; out->tm_isdst = 0;
    return out;
}
int nixel_tls_init(const nixel_tls_platform *p) {
    if (initialized || !p || !p->alloc || !p->free || !p->realloc ||
        !p->entropy || !p->unix_time || p->unix_time() <= 0) return NIXEL_TLS_ERROR;
    platform = *p;
    if (wolfSSL_Init() != WOLFSSL_SUCCESS) return NIXEL_TLS_ERROR;
    initialized = 1;
    return 0;
}
static int send_cb(WOLFSSL *ssl, char *buf, int size, void *ctx) {
    nixel_tls_client *c = ctx;
    int n;
    (void)ssl;
    n = c->send(c->io, (unsigned char *)buf, size);
    if (n == 0) return WOLFSSL_CBIO_ERR_WANT_WRITE;
    return n > 0 && n <= size ? n : WOLFSSL_CBIO_ERR_GENERAL;
}
static int recv_cb(WOLFSSL *ssl, char *buf, int size, void *ctx) {
    nixel_tls_client *c = ctx;
    int n;
    (void)ssl;
    n = c->receive(c->io, (unsigned char *)buf, size);
    if (n == 0) return WOLFSSL_CBIO_ERR_WANT_READ;
    if (n == -2) return WOLFSSL_CBIO_ERR_CONN_CLOSE;
    return n > 0 && n <= size ? n : WOLFSSL_CBIO_ERR_GENERAL;
}
void nixel_tls_client_destroy(nixel_tls_client *c) {
    if (!c) return;
    if (c->ssl) wolfSSL_free(c->ssl);
    if (c->ctx) wolfSSL_CTX_free(c->ctx);
    nixel_tls_free(c);
}
nixel_tls_client *nixel_tls_client_create(const unsigned char *ca, int ca_size,
    const char *hostname, nixel_tls_io send, nixel_tls_io receive, void *io) {
    nixel_tls_client *c;
    if (!initialized || !ca || ca_size <= 0 || !hostname || !*hostname ||
        !send || !receive || XSTRLEN(hostname) > 253 || nixel_tls_time(NULL) <= 0) return NULL;
    c = nixel_tls_alloc(sizeof(*c));
    if (!c) return NULL;
    *c = (nixel_tls_client){0};
    c->send = send; c->receive = receive; c->io = io;
    c->ctx = wolfSSL_CTX_new(wolfTLS_client_method());
    if (!c->ctx) goto fail;
    wolfSSL_CTX_set_verify(c->ctx, WOLFSSL_VERIFY_PEER, NULL);
    if (wolfSSL_CTX_SetMinVersion(c->ctx, WOLFSSL_TLSV1_2) != WOLFSSL_SUCCESS ||
        wolfSSL_CTX_load_verify_buffer(c->ctx, ca, ca_size, WOLFSSL_FILETYPE_PEM)
        != WOLFSSL_SUCCESS) goto fail;
    wolfSSL_SetIOSend(c->ctx, send_cb); wolfSSL_SetIORecv(c->ctx, recv_cb);
    c->ssl = wolfSSL_new(c->ctx);
    if (!c->ssl) goto fail;
    wolfSSL_SetIOWriteCtx(c->ssl, c); wolfSSL_SetIOReadCtx(c->ssl, c);
    if (wolfSSL_check_domain_name(c->ssl, hostname) != WOLFSSL_SUCCESS ||
        wolfSSL_UseSNI(c->ssl, WOLFSSL_SNI_HOST_NAME, hostname,
            (unsigned short)XSTRLEN(hostname)) != WOLFSSL_SUCCESS) goto fail;
    return c;
fail:
    nixel_tls_client_destroy(c);
    return NULL;
}
static int result(nixel_tls_client *c, int ret) {
    if (ret > 0) { c->error = 0; return ret; }
    c->error = wolfSSL_get_error(c->ssl, ret);
    if (c->error == WOLFSSL_ERROR_WANT_READ || c->error == WOLFSSL_ERROR_WANT_WRITE)
        return NIXEL_TLS_AGAIN;
    if (c->error == WOLFSSL_ERROR_ZERO_RETURN) return 0;
    return NIXEL_TLS_ERROR;
}
int nixel_tls_handshake(nixel_tls_client *c) {
    int r;
    if (!c) return NIXEL_TLS_ERROR;
    r = result(c, wolfSSL_connect(c->ssl));
    return r == 0 ? NIXEL_TLS_ERROR : r;
}
int nixel_tls_read(nixel_tls_client *c, void *p, int n) {
    return c && p && n > 0 ? result(c, wolfSSL_read(c->ssl, p, n)) : NIXEL_TLS_ERROR;
}
int nixel_tls_write(nixel_tls_client *c, const void *p, int n) {
    return c && p && n > 0 ? result(c, wolfSSL_write(c->ssl, p, n)) : NIXEL_TLS_ERROR;
}
int nixel_tls_close(nixel_tls_client *c) {
    int ret;
    if (!c) return NIXEL_TLS_ERROR;
    ret = wolfSSL_shutdown(c->ssl);
    if (ret == WOLFSSL_SUCCESS || ret == WOLFSSL_SHUTDOWN_NOT_DONE) return 1;
    return result(c, ret);
}
int nixel_tls_last_error(const nixel_tls_client *c) {
    return c ? c->error : NIXEL_TLS_ERROR;
}

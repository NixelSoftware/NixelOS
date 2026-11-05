#ifndef NIXEL_TLS_H
#define NIXEL_TLS_H
#include <stddef.h>
/* Process-local, single-threaded library. Configure once before creating clients.
 * Entropy must fill the entire buffer using a cryptographic source, returning 0
 * on success. unix_time returns UTC seconds since 1970, or -1 on failure.
 * Allocator callbacks must implement the usual malloc/free/realloc contract. */
typedef struct {
    void *(*alloc)(size_t);
    void (*free)(void *);
    void *(*realloc)(void *, size_t);
    int (*entropy)(unsigned char *, unsigned int);
    long (*unix_time)(void);
} nixel_tls_platform;
typedef struct nixel_tls_client nixel_tls_client;
/* Transport callbacks return bytes transferred, 0 for retry, -1 for failure,
 * or -2 for receive EOF. They must never report more bytes than requested. */
typedef int (*nixel_tls_io)(void *, unsigned char *, int);
enum { NIXEL_TLS_ERROR = -1, NIXEL_TLS_AGAIN = -2 };
int nixel_tls_init(const nixel_tls_platform *platform);
/* A nonempty PEM trust bundle and DNS hostname are required. */
nixel_tls_client *nixel_tls_client_create(const unsigned char *ca, int ca_size,
    const char *hostname, nixel_tls_io send, nixel_tls_io receive, void *io);
/* handshake: 1 complete, AGAIN retry after yielding, ERROR failed.
 * read/write: positive byte count, AGAIN retry, ERROR failed; read 0 = TLS EOF.
 * Keep the same write buffer and length across AGAIN retries. */
int nixel_tls_handshake(nixel_tls_client *client);
int nixel_tls_read(nixel_tls_client *client, void *data, int size);
int nixel_tls_write(nixel_tls_client *client, const void *data, int size);
/* Retry close on AGAIN to send close_notify before destroying the client. */
int nixel_tls_close(nixel_tls_client *client);
int nixel_tls_last_error(const nixel_tls_client *client);
void nixel_tls_client_destroy(nixel_tls_client *client);
#endif

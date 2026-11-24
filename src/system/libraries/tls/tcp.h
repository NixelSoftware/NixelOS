#ifndef NIXEL_TLS_TCP_H
#define NIXEL_TLS_TCP_H
#include "tls.h"
/* Pass a pointer to the connected TCP handle as the I/O context. The caller
 * owns the socket and must keep the handle alive until client destruction. */
int nixel_tls_tcp_send(void *handle, unsigned char *data, int size);
int nixel_tls_tcp_receive(void *handle, unsigned char *data, int size);
#endif

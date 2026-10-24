#ifndef NIXPLORER_PLATFORM_TCP_H
#define NIXPLORER_PLATFORM_TCP_H
/* Transport boundary only: no HTTP parsing or request policy in the OS. */
enum { NIX_TCP_CLOSED = 0, NIX_TCP_ESTABLISHED = 4, NIX_TCP_CLOSE_WAIT = 7 };
int nix_tcp_resolve(const char *host, unsigned *address);
int nix_tcp_connect(unsigned address, unsigned port);
int nix_tcp_state(unsigned handle);
int nix_tcp_send(unsigned handle, const void *data, unsigned length);
int nix_tcp_read(unsigned handle, void *out, unsigned capacity);
void nix_tcp_close(unsigned handle);
void nix_tcp_yield(void);
unsigned nix_tcp_ticks(void);
#endif

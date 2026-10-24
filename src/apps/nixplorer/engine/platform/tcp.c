#include "tcp.h"
#include "nixel.h"
_Static_assert((int)NIX_TCP_CLOSED == (int)USER_TCP_CLOSED &&
               (int)NIX_TCP_ESTABLISHED == (int)USER_TCP_ESTABLISHED &&
               (int)NIX_TCP_CLOSE_WAIT == (int)USER_TCP_CLOSE_WAIT, "native TCP state ABI");
int nix_tcp_resolve(const char *host, unsigned *address) {
    int32_t handle = sys_dns_start(host);
    if (handle < 0) return 0;
    for (;;) {
        sys_yield();
        int32_t result = sys_dns_poll((uint32_t)handle, address);
        if (result) return result == 1;
        /* The OS resolver owns its timeout and releases the handle on failure.
         * Do not abandon a pending lookup: that would retain the global slot. */
    }
}
int nix_tcp_connect(unsigned address, unsigned port) { return sys_tcp_connect(address, port); }
int nix_tcp_state(unsigned handle) { return sys_tcp_state(handle); }
int nix_tcp_send(unsigned handle, const void *data, unsigned length) { return sys_tcp_send(handle, data, length); }
int nix_tcp_read(unsigned handle, void *out, unsigned cap) { return sys_tcp_read(handle, out, cap); }
void nix_tcp_close(unsigned handle) { (void)sys_tcp_close(handle); }
void nix_tcp_yield(void) { sys_yield(); }
unsigned nix_tcp_ticks(void) { return sys_get_ticks(); }

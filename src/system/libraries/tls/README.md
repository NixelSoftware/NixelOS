# TLS for Nixel

A reusable, freestanding i386 user-space TLS client library, using wolfSSL
5.9.4 from `documentation/linux-for-ref/wolfssl-master`. Mbed TLS was also
examined, but its `tf-psa-crypto` dependency is absent in the reference tree.
The installed sources build independently of `documentation/linux-for-ref`.

```
tls.h, tls.c       Nixel client API and platform callbacks
tcp.h, tcp.c       Adapter for Nixel's user-space TCP syscalls
user_settings.h   Shared wolfSSL feature configuration
runtime.c         Private, prefixed string functions
div64.c           Weak compiler helpers for freestanding i386 division
compat/           Private C headers, confined to the library build
vendor/wolfssl/   Upstream headers, TLS and required wolfCrypt sources, licenses
tls.mk            Build integration
tests/            Host interoperability and verification tests
```

`make tls` builds `src/system/libraries/tls/libnixeltls.a`; the normal `make`
also builds it. Link the archive after an application's objects. Include
`tls.h` and optionally `tcp.h`; consumers of this API do not need wolfSSL
headers or feature defines. Direct wolfSSL consumers must use `TLS_CPPFLAGS`
from `tls.mk` so their configuration matches the archive.

The configuration enables TLS 1.2 and 1.3, AES-GCM, SHA-2, RSA and ECC, SNI,
and peer certificate, hostname and validity-date checks. RSA blinding and ECC
timing resistance remain enabled. Legacy SSL/TLS, server mode, DTLS, native
sockets, filesystem access, threads and session caching are disabled. TLS runs
in the application; TCP remains in the kernel.

## Using the client API

1. Call `nixel_tls_init` once with allocator, cryptographic entropy and UTC
   Unix-time callbacks. This library is single-threaded and process-local;
   serialize calls and do not replace the platform while objects exist.
2. Resolve the hostname, connect a TCP socket and wait for `TCP_ESTABLISHED`.
3. Call `nixel_tls_client_create` with an explicit PEM CA bundle, the DNS
   hostname, `nixel_tls_tcp_send`, `nixel_tls_tcp_receive`, and a pointer to
   the `uint32_t` socket handle. Keep that handle storage alive. IP-literal
   certificate matching is outside this wrapper's DNS-hostname contract.
4. Call `nixel_tls_handshake` until it returns 1. On `NIXEL_TLS_AGAIN`, yield
   and retry with an application deadline. A fatal result requires destroying
   the client; use `nixel_tls_last_error` to retrieve wolfSSL's error code.
5. Read/write through the TLS API, advancing by the returned byte count.
   Retry `NIXEL_TLS_AGAIN`; preserve the write buffer and length on retries.
   Read returns 0 for authenticated TLS shutdown. Transport EOF is an error.
6. Call `nixel_tls_close`, retrying `NIXEL_TLS_AGAIN` with a deadline, then
   destroy the client and close the TCP handle. Destroy releases memory
   without performing I/O and never closes the underlying socket.

Custom transport callbacks may replace the syscall adapter; their return
conventions are documented in `tls.h`. The adapter accounts for TCP's boolean
send result, maximum payload size, receive limit and buffered data after FIN.

No trust roots or test credentials are installed in Nixel. Applications must
supply their CA bundle and platform callbacks. In particular, timer ticks,
`rand()` and MAC addresses are not cryptographic entropy. Initialization fails
without an available clock and handshakes fail if entropy cannot be obtained.
The platform time ABI is signed 32-bit seconds, supported through January 2038.
Existing curl, browser and IRC clients have not been switched to this API;
they retain their existing protocol support until their platform and trust
configuration is connected.

## Validation

Run `make test-tls` (host C compiler, Python 3 with SSL support, OpenSSL CLI,
GNU ld and nm required). Tests check that the i386 archive has no unresolved
symbols, then compile the same TLS configuration for the host and exchange
application data with Python/OpenSSL peers over local socket pairs. Coverage:
TLS 1.2 and 1.3, fragmented nonblocking I/O, hostname mismatch, untrusted CA,
entropy failure, expired certificate and unavailable clock. These are host
interoperability tests; the library has not yet been exercised in a booted
Nixel guest.

## Upstream sources

`vendor/wolfssl` preserves upstream source files without Nixel edits. It contains
all upstream public/internal headers and TLS sources, plus the selected
wolfCrypt C files and included implementation fragments required by this build.
The reference version identifies itself as 5.9.4; it is a development snapshot,
not a claim of a validated upstream release. `div64.c` reuses Nixel's compiler
helpers from `src/apps/syndig/compat/div64.c`, with weak linkage to coexist with
application runtime helpers. wolfSSL copyright and GPLv3/commercial licensing
information are retained in `vendor/wolfssl/COPYING` and `LICENSING`.

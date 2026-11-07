# Kernel entropy for WPA2

`kernel_entropy` supplies fresh WPA2 SNonces. It uses CPUID-checked RDRAND with
bounded hardware retries, falling back to the upstream CPU Jitter RNG when
RDRAND is unavailable or fails. No wall-clock, MAC, password, fixed seed or
ordinary PRNG supplies cryptographic entropy.

The fallback is Jitterentropy 3.6.3, pinned from:
https://github.com/smuellerDD/jitterentropy-library/releases/tag/v3.6.3

Upstream release archive SHA-256:
`9b3819e275e6473a94c70cfaae7cc2a87a11f60b862e694745daa5d232b0eea3`

The vendored core is unchanged. The only change to its public header selects
our freestanding platform hooks with `JENT_NIXEL`. The licenses and copyright
notices are retained in `vendor/jitterentropy`. The BSD licensing option is used.

Platform hooks provide TSC timing, zero-filled pinned physical RAM, secure erasure
and unsigned 64-bit rotation. Memory-size selection retains the upstream default
128 KiB when cache information is unavailable. The existing freestanding division
helpers are linked for the upstream 64-bit arithmetic on i386.

The core is compiled with `-O0 -fwrapv`, as upstream requires. Startup and runtime
health tests, SHA3 self-tests, lag prediction, memory noise and oversampling rate
3 remain enabled. `JENT_FORCE_FIPS` enables strict health failure reporting; this
port is not a FIPS certification claim. No timer-less thread backend is enabled.
A CPU without TSC and RDRAND cannot use this source.

A private mutex serializes the collector. It is initialized lazily before Wi-Fi
firmware startup, outside the shared UI/network mutex. New nonce requests collect
fresh entropy. Source or allocation failures erase the entire requested output;
a runtime health failure also destroys and erases the collector. The next explicit
connection attempt may initialize it again. Calls are limited to 1–256 bytes and
must run in process context. Kernel RAM is supervisor-only and is not swapped.

`make test-entropy` exercises the real upstream collector with a host TSC and
injected source-selection/failure hooks: no RDRAND, failed RDRAND, absent sources,
startup and runtime stuck timers, allocation failure, output erasure and the
mandatory optimization guard. Fixture values are confined to test code.

`make test-entropy-qemu` runs the production architecture hooks, collector and
physical-page allocator at ring 0 under a Pentium III CPU model without RDRAND.
It obtains two different nonzero 32-byte outputs. These tests verify integration
and error handling, not an entropy-rate certification for every physical CPU.

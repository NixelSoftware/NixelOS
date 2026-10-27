Vendored from https://github.com/lieff/minimp3
Commit: ea99364f61c14656440e8d77e9c233ccf3124633
License: CC0-1.0 (see LICENSE).

minimp3.h is unmodified. ../mp3.c uses the scalar, Layer III-only decoder
with signed 16-bit output. ../compat supplies its freestanding memory routines;
no hosted libc, allocator, filesystem API, SIMD, or libm is required.

Playback primes one decoded frame, then refills the 16 KiB compressed input
buffer on demand. There is no duration pre-scan or second decoding pass.
MP3 progress follows compressed bytes consumed; total duration becomes known
at EOF. Sample-rate changes are rejected as encountered during playback.

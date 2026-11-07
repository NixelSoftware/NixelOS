/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef NIXEL_ENTROPY_H
#define NIXEL_ENTROPY_H
#include "../types.h"
/* Process context only; serialized internally. Zeroes valid output buffers on source failure.
 * Uses RDRAND when available, otherwise health-tested CPU jitter entropy. */
int kernel_entropy(void* context, uint8_t* out, unsigned length);
#endif

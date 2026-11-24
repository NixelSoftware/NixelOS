/* SPDX-License-Identifier: BSD-3-Clause */
#include "entropy.h"
#include "../mutex.h"
#include "vendor/jitterentropy/jitterentropy.h"
/* Architecture hooks are separate so failure paths can be tested without
 * pretending deterministic input is a production entropy source. */
unsigned entropy_cpu_features(void); /* bit 0: TSC, bit 1: RDRAND */
int entropy_rdrand(uint32_t* value);
static mutex_t entropy_mutex;
static struct rand_data* collector;
static void erase(uint8_t* p, unsigned n) { while (n--) *(volatile uint8_t*)p++ = 0; }
int kernel_entropy(void* context, uint8_t* out, unsigned length) {
    (void)context;
    if (!out || !length || length > 256) return -1;
    MUTEX_GUARD(entropy_mutex);
    unsigned features = entropy_cpu_features();
    if (features & 2) {
        unsigned offset = 0;
        while (offset < length) {
            uint32_t word = 0;
            if (entropy_rdrand(&word)) break;
            for (unsigned i = 0; i < 4 && offset < length; i++, offset++) {
                out[offset] = word; word >>= 8;
            }
        }
        if (offset == length) return 0;
        erase(out,length);
    }
    if (!(features & 1)) { erase(out,length); return -1; }
    if (!collector) {
        /* Same oversampling/flags for startup tests and collector, as required
         * upstream. Preserve all default health tests and memory access. */
        unsigned flags = JENT_FORCE_FIPS;
        if (jent_entropy_init_ex(3,flags)) { erase(out,length); return -1; }
        collector = jent_entropy_collector_alloc(3,flags);
        if (!collector) { erase(out,length); return -1; }
    }
    if (jent_read_entropy(collector,(char*)out,length) != (ssize_t)length) {
        erase(out,length);
        jent_entropy_collector_free(collector); collector = NULL;
        return -1;
    }
    return 0;
}

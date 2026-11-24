/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NIXEL_WIFI_CCMP_H
#define NIXEL_WIFI_CCMP_H
#include "wpa2.h"
typedef struct {
    uint8_t key[16], own[6], peer[6];
    uint64_t next_tx_pn, rx_pn[17];
    uint8_t active, key_id;
} wifi_ccmp_key_t;
/* Zero-initialize once. Reinstalling the same key/identity preserves PNs.
 * Caller must not clear an active context and reinstall the same key. */
int wifi_ccmp_install(wifi_ccmp_key_t*, const uint8_t key[16], unsigned key_id,
                        const uint8_t own[6], const uint8_t peer[6]);
/* Three-address data/QoS-data frames only. IV/MIC are inserted/removed.
 * No fragmentation, WDS or HT-control headers. Buffers must not alias.
 * Decrypt commits replay state only after authentication; failed plaintext is
 * never released. Per-TID replay windows are strictly increasing. */
int wifi_ccmp_encrypt(wifi_ccmp_key_t*, const uint8_t* frame, unsigned length,
                        uint8_t* out, unsigned capacity, unsigned* written);
int wifi_ccmp_decrypt(wifi_ccmp_key_t*, const uint8_t* frame, unsigned length,
                        uint8_t* out, unsigned capacity, unsigned* written);
int wifi_wpa2_unwrap(const uint8_t kek[16], const uint8_t* wrapped, unsigned length,
                       uint8_t* out, unsigned capacity, unsigned* written);
#endif

/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NIXEL_WIFI_WPA2_H
#define NIXEL_WIFI_WPA2_H
#include "../../../types.h"
/* Return 0 on success, -1 on invalid input/crypto failure. Buffers must not
 * alias input. No global TLS state, allocator or entropy source is used.
 * These primitives do not implement the EAPOL handshake or install keys. */
int wifi_wpa2_pmk(const uint8_t* password, unsigned password_length,
                   const uint8_t* ssid, unsigned ssid_length, uint8_t pmk[32]);
int wifi_wpa2_ptk(const uint8_t pmk[32], const uint8_t own_mac[6], const uint8_t ap_mac[6],
                   const uint8_t snonce[32], const uint8_t anonce[32], uint8_t ptk[64]);
/* RSN EAPOL-Key descriptor version 2 only (HMAC-SHA1-128); includes the EAPOL
 * header. The MIC field is treated as zero, never changed in the input. */
int wifi_wpa2_eapol_mic(const uint8_t kck[16], const uint8_t* frame,
                         unsigned length, uint8_t mic[16]);
int wifi_wpa2_eapol_verify(const uint8_t kck[16], const uint8_t* frame, unsigned length);
void wifi_crypto_erase(void* memory, unsigned length);
#endif

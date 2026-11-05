/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NIXEL_WIFI_HANDSHAKE_H
#define NIXEL_WIFI_HANDSHAKE_H
#include "ccmp.h"
#define WIFI_EAPOL_MAX 611u
typedef int (*wifi_nonce_fn)(void*, uint8_t* out, unsigned length);
typedef struct {
    uint8_t pmk[32], ptk[64], own[6], peer[6], snonce[32], anonce[32], replay[8];
    uint8_t ap_rsn[257], m2[121], m3[WIFI_EAPOL_MAX], m4[99];
    unsigned ap_rsn_length, m3_length, started_ms;
    uint8_t initialized, phase, authorized;
    wifi_ccmp_key_t pairwise, group;
} wifi_wpa2_station_t;
/* Zero-initialize once. Call only after association negotiated RSN/CCMP/PSK without required PMF.
 * ap_rsn is the exact complete RSN IE from that AP's beacon/association.
 * WPA Enterprise, WPA1/TKIP, PMF and SAE are not accepted here. */
int wifi_wpa2_station_init(wifi_wpa2_station_t*, const uint8_t pmk[32],
                             const uint8_t own[6], const uint8_t peer[6],
                             const uint8_t* ap_rsn, unsigned rsn_length);
/* Full EAPOL frame, peer bound. 1 = response ready, -1 = invalid/failure.
 * Strong entropy callback is required for each new M1, never for duplicates.
 * Pairwise/GTK contexts become authorized atomically after verified M3.
 * Callers must gate ordinary data on authorized and deliver response via TX. */
int wifi_wpa2_station_receive(wifi_wpa2_station_t*, const uint8_t sender[6],
                                const uint8_t* frame, unsigned length, unsigned now_ms,
                                wifi_nonce_fn entropy, void* entropy_context,
                                uint8_t* response, unsigned capacity, unsigned* written);
/* Expire unfinished handshakes after 5 seconds, clearing all keys/port state.
 * Caller then disconnects/reassociates; monotonic milliseconds may wrap. */
int wifi_wpa2_station_authorized(const wifi_wpa2_station_t*);
int wifi_wpa2_station_encrypt(wifi_wpa2_station_t*, const uint8_t*, unsigned, uint8_t*, unsigned, unsigned*);
int wifi_wpa2_station_decrypt(wifi_wpa2_station_t*, const uint8_t*, unsigned, uint8_t*, unsigned, unsigned*);
int wifi_wpa2_station_expire(wifi_wpa2_station_t*, unsigned now_ms);
void wifi_wpa2_station_clear(wifi_wpa2_station_t*);
#endif

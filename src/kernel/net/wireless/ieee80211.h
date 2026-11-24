#ifndef NIXEL_IEEE80211_H
#define NIXEL_IEEE80211_H
#include "../../types.h"
/* Infrastructure station, three-address frames, no fragmentation/A-MSDU/WDS.
 * Input RX has no FCS and has already passed link decryption/authentication.
 * Buffers must not overlap; no frame is partially published on failure. */
int wifi_ethernet_encapsulate(const uint8_t own[6], const uint8_t ap[6], unsigned sequence,
                               const uint8_t* ethernet, unsigned length,
                               uint8_t* frame, unsigned capacity, unsigned* written);
int wifi_ethernet_decapsulate(const uint8_t own[6], const uint8_t ap[6],
                               const uint8_t* frame, unsigned length,
                               uint8_t* ethernet, unsigned capacity, unsigned* written);
#endif

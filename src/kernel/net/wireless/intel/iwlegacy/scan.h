/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NIXEL_IWLEGACY_SCAN_H
#define NIXEL_IWLEGACY_SCAN_H
#include "command.h"
#define IL_SCAN_NETWORKS 32u
typedef enum { IL_SECURITY_OPEN, IL_SECURITY_WPA2_PSK, IL_SECURITY_UNSUPPORTED } il_security_t;
typedef struct {
    uint8_t bssid[6], ssid[32], ssid_length, channel, band;
    int16_t signal_dbm;
    uint16_t capability, beacon_interval;
    il_security_t security;
    uint8_t timestamp[8];
    uint8_t rates[12], rate_count;
    uint8_t rsn[257];
    uint16_t rsn_length;
} il_network_t;
typedef struct {
    il_network_t networks[IL_SCAN_NETWORKS];
    unsigned count, malformed, dropped;
    uint8_t active_band, complete, expected_channels, completed_channels;
    int error;
    uint8_t allowed_channels[256];
} il_scan_results_t;
/* Passive only: no probe transmission and no passive-to-active promotion.
 * Scan lists are derived exclusively from valid EEPROM channel permissions. */
int iwlegacy_scan(iwlegacy_info_t* info, il_scan_results_t* results);
int il3945_scan_configure(const il_io_t*, il_rx_queue_t*, il_command_queue_t*,
                            const il3945_eeprom_t*, unsigned band, unsigned channel);
int il3945_scan_build(const il3945_eeprom_t* eeprom, unsigned band,
                       uint8_t* out, unsigned capacity, unsigned* length);
void il3945_scan_event(void*, uint8_t command, uint8_t flags, uint16_t sequence,
                        const uint8_t* payload, unsigned length);
int il3945_scan_run(const il_io_t* io, il_rx_queue_t* rx, il_command_queue_t* commands,
                     const il3945_eeprom_t* eeprom, il_scan_results_t* results);
#endif

/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NIXEL_IWLEGACY_STATION_H
#define NIXEL_IWLEGACY_STATION_H
#include "scan.h"
#include "tx.h"
#include "../../security/handshake.h"
#include "../../../../hal/net_types.h"
typedef enum {
    IL_STATION_IDLE, IL_STATION_AUTH_WAIT, IL_STATION_AUTHENTICATED,
    IL_STATION_ASSOC_WAIT, IL_STATION_ASSOCIATED, IL_STATION_SECURING
} il_station_phase_t;
typedef struct {
    il_io_t io;
    il_rx_queue_t rx;
    il_command_queue_t commands;
    il_tx_queue_t tx;
    il3945_eeprom_t eeprom;
    il_network_t ap;
    wifi_wpa2_station_t security;
    wifi_nonce_fn entropy;
    void* entropy_context;
    net_receive_handler_t receive;
    net_diagnostics_t diagnostics;
    uint8_t frames[8][2304];
    uint16_t lengths[8];
    unsigned head, count, sequence, now_ms, tx_started, last_rx, thermal_ms;
    il_station_phase_t phase;
    unsigned last_beacon,last_ack,probe_at,probe_attempts;
    uint16_t rx_sequence[17];
    uint32_t rx_sequence_valid;
    uint8_t probing,probe_pending,bss_dirty;
    int error, temperature;
    uint32_t runtime_irq, runtime_fh, error_log_base;
    il3945_error_reply_t error_reply;
    il3945_tx_snapshot_t last_data_tx;
    il3945_error_log_t firmware_error;
    int runtime_error;
    unsigned failure_phase;
    uint8_t failure_security_phase,failure_authorized,failure_active;
    il3945_tx_snapshot_t tx_history[8];
    unsigned tx_history_next,tx_history_count;
    il_connection_reject_t connection_reject;
    uint16_t management_status, association_capability, association_aid;
    uint16_t aid;
    uint8_t awake_flags; /* PCI_PM (bit 3), derived from PCIe Link Control. */
    uint8_t active, processing, tx_plcp, fallback, ofdm_basic, cck_basic, management[512];
} il_station_t;
/* Caller serializes access, supplies initialized firmware and pinned rings.
 * Explicit software crypto; firmware decryption stays disabled. */
int il3945_station_connect(il_station_t*, const il3945_eeprom_t*, const il_network_t*,
                              const uint8_t* password, unsigned password_length,
                              wifi_nonce_fn, void* entropy_context);
int il3945_station_poll(il_station_t*, unsigned now_ms);
int il3945_station_send(il_station_t*, const uint8_t*, unsigned);
void il3945_station_clear(il_station_t*);
void il3945_station_leave(il_station_t*);
/* Native lifecycle entry points. Invoke connect outside kernel_mutex. The HAL
 * gates poll/send until connect finishes, then serializes with kernel_mutex. */
int iwlegacy_connect(iwlegacy_info_t*, const il_network_t*, const uint8_t*, unsigned,
                       wifi_nonce_fn, void*);
int iwlegacy_disconnect(void);
int iwlegacy_poll(unsigned elapsed_ms);
int iwlegacy_send(const uint8_t*,unsigned);
int iwlegacy_connected(void);
void iwlegacy_set_receiver(net_receive_handler_t);
void iwlegacy_get_diagnostics(net_diagnostics_t*);
void iwlegacy_get_runtime_status(iwlegacy_info_t*);
#endif

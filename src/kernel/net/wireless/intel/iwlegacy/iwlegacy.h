/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NIXEL_IWLEGACY_H
#define NIXEL_IWLEGACY_H
#include "../../../../drivers/pci.h"
#define IL3945_EEPROM_SIZE 1024u
#define IL3945_INST_MAX 0x14000u
#define IL3945_DATA_MAX 0x8000u
#define IL3945_BOOT_MAX 1024u
#define IL3945_CHANNEL_COUNT 56u

typedef enum {
    IL_ABSENT, IL_PCI_FOUND, IL_DEVICE_BUSY, IL_ACCESS_FAILED,
    IL_CLOCK_TIMEOUT, IL_EEPROM_FAILED, IL_EEPROM_READY,
    IL_FIRMWARE_MISSING, IL_FIRMWARE_FAILED, IL_FIRMWARE_READY, IL_SCAN_FAILED, IL_SCAN_COMPLETE, IL_CONNECTED, IL_CONNECTION_FAILED
} iwlegacy_state_t;
typedef struct {
    uint8_t number, band, flags;
    int8_t max_power_dbm;
} il3945_channel_t;
typedef struct {
    uint8_t mac[6], sku, radio_revision;
    uint16_t version, board_revision;
    il3945_channel_t channels[IL3945_CHANNEL_COUNT];
    uint8_t valid_channels;
    uint8_t calibration[320];
} il3945_eeprom_t;
typedef enum {
    IL_REJECT_NONE, IL_REJECT_AUTH_STATUS, IL_REJECT_ASSOC_STATUS,
    IL_REJECT_ASSOC_AID, IL_REJECT_ASSOC_IES, IL_REJECT_ASSOC_SECURITY,
    IL_REJECT_ASSOC_RATES
} il_connection_reject_t;
typedef struct {
    uint32_t type, info;
    uint16_t sequence;
    uint8_t command, valid;
} il3945_error_reply_t;
typedef struct {
    uint32_t flags, tfd_control, first_length, payload_length;
    uint16_t sequence, frame_length, frame_control, ether_type;
    uint32_t command_dma,payload_dma,status;
    uint8_t completed;
    uint8_t rate, station, security, valid;
} il3945_tx_snapshot_t;
/* Intel 3945 error table: count followed by seven-word records. */
typedef struct {
    uint32_t count, entry[7];
    int read_error;
} il3945_error_log_t;
typedef struct {
    uint16_t endpoint_before,endpoint_after,parent_before,parent_after;
    uint8_t valid,parent_present;
} il3945_link_power_t;
typedef struct {
    iwlegacy_state_t state;
    uint16_t vendor, device, subsystem_device;
    uint8_t pci_bus, pci_slot, pci_function, revision, bg_only, rfkill;
    uint32_t hardware_revision, firmware_version;
    int error;
    il3945_link_power_t link_power;
    int operation_error; /* Original failure before DMA cleanup. */
    uint8_t stop_failures; /* RX=1, TX=2, global stop/reset=4, PCI master=8. */
    uint8_t init_alive, runtime_alive, boot_stage;
    uint8_t last_command, command_pending, command_poisoned;
    uint32_t command_gp1, command_tx_config, command_tx_status;
    uint32_t command_gp, command_fh, command_tx_base, command_scd_mode, command_scd_fifos;
    unsigned command_rx_read, command_rx_closed;
    unsigned command_response_length;
    uint32_t command_response_status;
    uint8_t command_response_id, command_response_flags;
    unsigned scan_completion_length;
    uint8_t scan_expected_channels, scan_completed_channels, scan_completion_status;
    il3945_error_reply_t error_reply;
    il3945_tx_snapshot_t last_data_tx;
    il3945_error_log_t firmware_error;
    int runtime_error;
    unsigned connection_phase;
    uint8_t security_phase,port_authorized,port_active;
    unsigned tx_history_count;
    il3945_tx_snapshot_t tx_history[8];
    il_connection_reject_t connection_reject;
    uint16_t management_status, association_capability, association_aid;
    uint32_t runtime_irq, runtime_fh;
    uint8_t data_tx_pending, data_tx_poisoned;
    unsigned scan_count;
    il3945_eeprom_t eeprom;
} iwlegacy_info_t;
/* Native 3945BG/ABG PCI discovery; the station lifecycle is in station.h. */
void iwlegacy_probe(iwlegacy_info_t* info);
/* Load/check runtime firmware on an idle adapter, then stop it. No scan/TX.
 * Invoke outside kernel_mutex; firmware operations have a private mutex. */
int iwlegacy_firmware_check(iwlegacy_info_t* info);
const char* iwlegacy_description(iwlegacy_state_t state);
int il3945_matches(uint16_t vendor, uint16_t device);
int il3945_bg_only(uint16_t device, uint16_t subsystem);

typedef struct {
    void* context;
    uint32_t (*read32)(void*, uint32_t);
    void (*write32)(void*, uint32_t, uint32_t);
    void (*delay_us)(void*, uint32_t);
} il_io_t;
enum {
    IL_OK = 0, IL_INVALID = -1, IL_ACCESS = -2, IL_TIMEOUT = -3,
    IL_BAD_EEPROM = -4, IL_VERIFY = -5, IL_RFKILL = -6, IL_NOMEM = -7, IL_NO_FIRMWARE = -8, IL_DMA_STOP = -9
};
/* All register sequences require exclusive access to an idle 3945. No DMA
 * bus mastering or RF transmission is enabled by inspection. */
int il3945_inspect(const il_io_t* io, uint8_t bg_only, iwlegacy_info_t* info);
/* Encode C_TX_PWR_TBL using EEPROM interpolation and current raw temperature.
 * Recompute after RXON and whenever temperature changes by at least 6 units. */
int il3945_power_table(const il3945_eeprom_t*, unsigned band, unsigned channel,
                         int temperature, int user_limit_dbm, uint8_t out[52]);
int il3945_eeprom_parse(const uint8_t* data, uint32_t size,
                        uint8_t bg_only, il3945_eeprom_t* out);

typedef struct { const uint8_t* data; uint32_t length; } il_fw_section_t;
typedef struct {
    uint32_t version;
    il_fw_section_t runtime_code, runtime_data, init_code, init_data, bootstrap;
} il3945_firmware_t;
/* iwlwifi-3945-{2,1}.ucode, little-endian v1 header, no bundled firmware. */
int il3945_firmware_parse(const uint8_t* data, uint32_t size, il3945_firmware_t* out);
/* Caller supplies coherent, pinned 32-bit DMA buffers containing init images,
 * after RX/TX rings are configured and bus mastering enabled. No ALIVE wait or
 * embedded CPU release is performed here. On failure caller must reset the NIC
 * and stop DMA before releasing any buffers. Firmware must stay immutable. */
int il3945_load_bsm(const il_io_t* io, const uint8_t* firmware, uint32_t size,
                    uint32_t init_code_dma, uint32_t init_data_dma);
#endif

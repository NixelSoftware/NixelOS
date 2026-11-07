#include "wifi_commands.h"
#include "../../kernel/hal/wifi.h"

static void emit(shell_output_fn out, const char* text) { if (out) out(text); }
static void put_unsigned(shell_output_fn out, uint32_t value) {
    char text[11]; unsigned n=0;
    do { text[n++]='0'+value%10; value/=10; } while(value);
    while(n) { char digit[2]={text[--n],0}; emit(out,digit); }
}
static void put_hex32(shell_output_fn out, uint32_t value) {
    char text[11]="0x00000000";
    const char* digits="0123456789ABCDEF";
    for(unsigned i=0;i<8;i++) text[2+i]=digits[(value>>(28-4*i))&15];
    emit(out,text);
}
static void put_hex16(shell_output_fn out, uint16_t value) {
    char text[7]="0x0000";
    const char* digits="0123456789ABCDEF";
    for(unsigned i=0;i<4;i++) text[2+i]=digits[(value>>(12-4*i))&15];
    emit(out,text);
}
static const char* firmware_error_text(uint32_t code) {
    switch(code) {
    case 1: return "FAIL";
    case 2: return "BAD_PARAM";
    case 3: return "BAD_CHECKSUM";
    case 4: return "NMI_INTERRUPT";
    case 5: return "SYSASSERT";
    case 6: return "FATAL_ERROR";
    default: return "UNKNOWN";
    }
}
/* Read-only snapshot; status never probes, resets or starts the radio. Keep
 * output local to each call so concurrent terminals cannot redirect it. */
void shell_wifi_status(shell_output_fn out) {
    wifi_status_t status;
    sys_wifi_get_status(&status);
    emit(out,"Wi-Fi status\n  "); emit(out,sys_wifi_status_text()); emit(out,"\n");
    const iwlegacy_info_t* intel=&status.intel;
    if(intel->vendor) {
        emit(out,"  Driver: iwl3945 / iwlegacy");
        emit(out,"\n  Diagnostics format: 3");
        emit(out,"\n  Status: "); emit(out,iwlegacy_description(intel->state));
        if (intel->error) {
            emit(out,"\n  Error: ");
            if (intel->error < 0) emit(out,"-");
            put_unsigned(out,intel->error < 0 ? (unsigned)-intel->error : (unsigned)intel->error);
        }
        if (intel->stop_failures || intel->error == IL_DMA_STOP) {
            emit(out,"\n  Original operation error: ");
            if (intel->operation_error < 0) emit(out,"-");
            put_unsigned(out,intel->operation_error < 0 ? (unsigned)-intel->operation_error : (unsigned)intel->operation_error);
            emit(out,"\n  Stop failures:");
            if (intel->stop_failures & 1) emit(out," RX");
            if (intel->stop_failures & 2) emit(out," TX");
            if (intel->stop_failures & 4) emit(out," global/reset");
            if (intel->stop_failures & 8) emit(out," PCI-master");
        }
        if (intel->boot_stage) {
            emit(out,"\n  Firmware ALIVE: init="); put_unsigned(out,intel->init_alive);
            emit(out," runtime="); put_unsigned(out,intel->runtime_alive);
            emit(out,"\n  Boot stage: "); put_unsigned(out,intel->boot_stage);
            emit(out,"\n  Last command: "); put_hex16(out,intel->last_command);
            emit(out," pending="); put_unsigned(out,intel->command_pending);
            emit(out," poisoned="); put_unsigned(out,intel->command_poisoned);
            if (intel->error && intel->last_command && !intel->command_pending) {
                emit(out,"\n  Command reply ID/flags: "); put_hex16(out,intel->command_response_id);
                emit(out,"/"); put_hex16(out,intel->command_response_flags);
                emit(out,"\n  Command reply bytes: "); put_unsigned(out,intel->command_response_length);
                emit(out,"\n  Command reply status: "); put_hex32(out,intel->command_response_status);
            }
            if (intel->error && intel->last_command == 0x80 && !intel->command_pending) {
                emit(out,"\n  Scan completion bytes: "); put_unsigned(out,intel->scan_completion_length);
                emit(out,"\n  Scan channels completed/expected: "); put_unsigned(out,intel->scan_completed_channels);
                emit(out,"/"); put_unsigned(out,intel->scan_expected_channels);
                emit(out,"\n  Scan completion status: "); put_hex16(out,intel->scan_completion_status);
            }
            if (intel->command_poisoned) {
                emit(out,"\n  Command GP1: "); put_hex32(out,intel->command_gp1);
                emit(out,"\n  TX queue 4 config: "); put_hex32(out,intel->command_tx_config);
                emit(out,"\n  TX hardware status: "); put_hex32(out,intel->command_tx_status);
                emit(out,"\n  Command GP control: "); put_hex32(out,intel->command_gp);
                emit(out,"\n  Flow-handler IRQ status: "); put_hex32(out,intel->command_fh);
                emit(out,"\n  TX shared base: "); put_hex32(out,intel->command_tx_base);
                emit(out,"\n  Scheduler mode: "); put_hex32(out,intel->command_scd_mode);
                emit(out,"\n  Scheduler active FIFOs: "); put_hex32(out,intel->command_scd_fifos);
                emit(out,"\n  RX read/closed: "); put_unsigned(out,intel->command_rx_read);
                emit(out,"/"); put_unsigned(out,intel->command_rx_closed);
            }
        }
        if (intel->runtime_error || (intel->state==IL_CONNECTION_FAILED && intel->error)) {
            if (!intel->runtime_error) emit(out,"\n  Runtime snapshot: not captured (connection setup failure)");
            emit(out,"\n  Runtime IRQ/FH status: "); put_hex32(out,intel->runtime_irq);
            emit(out,"/"); put_hex32(out,intel->runtime_fh);
            emit(out,"\n  Data TX pending/poisoned: "); put_unsigned(out,intel->data_tx_pending);
            emit(out,"/"); put_unsigned(out,intel->data_tx_poisoned);
            if (intel->error_reply.valid) {
                emit(out,"\n  Firmware bad command/sequence: "); put_hex16(out,intel->error_reply.command);
                emit(out,"/"); put_hex16(out,intel->error_reply.sequence);
                emit(out,"\n  Firmware error type/info: "); put_hex32(out,intel->error_reply.type);
                emit(out,"/"); put_hex32(out,intel->error_reply.info);
            } else {
                emit(out,"\n  Firmware bad command/sequence: not captured");
                emit(out,"\n  Firmware error type/info: not captured");
            }
            if (intel->last_data_tx.valid) {
                emit(out,"\n  Last data TX sequence/length: "); put_hex16(out,intel->last_data_tx.sequence);
                emit(out,"/"); put_unsigned(out,intel->last_data_tx.frame_length);
                emit(out,"\n  Data TX flags/rate/station/security: "); put_hex32(out,intel->last_data_tx.flags);
                emit(out,"/"); put_hex16(out,intel->last_data_tx.rate);
                emit(out,"/"); put_unsigned(out,intel->last_data_tx.station);
                emit(out,"/"); put_hex16(out,intel->last_data_tx.security);
                emit(out,"\n  Data TFD control/first/payload: "); put_hex32(out,intel->last_data_tx.tfd_control);
                emit(out,"/"); put_unsigned(out,intel->last_data_tx.first_length);
                emit(out,"/"); put_unsigned(out,intel->last_data_tx.payload_length);
            } else {
                emit(out,"\n  Last data TX sequence/length: not captured");
                emit(out,"\n  Data TX flags/rate/station/security: not captured");
                emit(out,"\n  Data TFD control/first/payload: not captured");
            }
            if (intel->firmware_error.read_error) emit(out,"\n  Firmware error log: unavailable");
            else {
                emit(out,"\n  Firmware error entries: "); put_unsigned(out,intel->firmware_error.count);
                if (intel->firmware_error.count) {
                    emit(out,"\n  Firmware error code: "); put_hex32(out,intel->firmware_error.entry[0]);
                    emit(out," ("); emit(out,firmware_error_text(intel->firmware_error.entry[0])); emit(out,")");
                    emit(out,"\n  Firmware assert PC/line: "); put_hex32(out,intel->firmware_error.entry[2]);
                    emit(out,"/"); put_unsigned(out,intel->firmware_error.entry[6]);
                    emit(out,"\n  Firmware blink2/ilink1/nmiPC: ");
                    put_hex32(out,intel->firmware_error.entry[3]); emit(out,"/");
                    put_hex32(out,intel->firmware_error.entry[4]); emit(out,"/");
                    put_hex32(out,intel->firmware_error.entry[5]);
                }
            }
        }
        if (intel->state == IL_SCAN_COMPLETE) {
            emit(out,"\n  Networks found: "); put_unsigned(out,intel->scan_count);
        }
        if (intel->firmware_version) {
            emit(out,"\n  Firmware version: ");
            for (int shift = 24; shift >= 0; shift -= 8) {
                if (shift != 24) emit(out,".");
                put_unsigned(out,(intel->firmware_version >> shift) & 255);
            }
        }
        if (intel->eeprom.version) {
            emit(out,"\n  MAC: ");
            for (unsigned octet = 0; octet < 6; octet++) {
                const char* hex = "0123456789ABCDEF";
                uint8_t byte = intel->eeprom.mac[octet];
                char text[3] = {hex[byte >> 4], hex[byte & 15], 0};
                if (octet) emit(out,":");
                emit(out,text);
            }
            emit(out,"\n  EEPROM version: "); put_hex16(out,intel->eeprom.version);
            emit(out,"\n  Board revision: "); put_hex16(out,intel->eeprom.board_revision);
            emit(out,"\n  Allowed channels: "); put_unsigned(out,intel->eeprom.valid_channels);
            emit(out,"\n  Radio switch: "); emit(out,intel->rfkill ? "Blocked" : "Enabled");
        }

        emit(out,"\n");
    } else if(status.adapter.vendor) {
        const b43_info_t* adapter=&status.adapter;
        emit(out,adapter->state == B43_LEGACY ? "  Driver: b43legacy\n  Status: " : "  Driver: b43\n  Status: "); emit(out,b43_probe_description(adapter->state));
        emit(out,"\n  Chip: "); put_hex16(out,adapter->chip);
        emit(out,"\n  Core revision: "); put_unsigned(out,adapter->core_revision);
        emit(out,"\n");
    }
}

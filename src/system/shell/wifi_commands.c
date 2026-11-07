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
/* Read-only snapshot; status never probes, resets or starts the radio. Keep
 * output local to each call so concurrent terminals cannot redirect it. */
void shell_wifi_status(shell_output_fn out) {
    wifi_status_t status;
    sys_wifi_get_status(&status);
    emit(out,"Wi-Fi status\n  "); emit(out,sys_wifi_status_text()); emit(out,"\n");
    const iwlegacy_info_t* intel=&status.intel;
    if(intel->vendor) {
        emit(out,"  Driver: iwl3945 / iwlegacy");
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

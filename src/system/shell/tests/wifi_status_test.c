#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../wifi_commands.h"
#include "../../../kernel/hal/wifi.h"
static wifi_status_t snapshot;
static char captured[4096];
void sys_wifi_get_status(wifi_status_t* out) { *out=snapshot; }
const char* sys_wifi_status_text(void) { return snapshot.intel.vendor ? "Wi-Fi firmware command timed out." : "No Wi-Fi adapter found."; }
const char* iwlegacy_description(iwlegacy_state_t s) { (void)s; return "Intel firmware command failed"; }
const char* b43_probe_description(b43_probe_state_t s) { (void)s; return "Broadcom radio unavailable"; }
static void output(const char* text) { assert(strlen(captured)+strlen(text)<sizeof(captured)); strcat(captured,text); }
int main(void) {
    shell_wifi_status(output);
    assert(strstr(captured,"No Wi-Fi adapter found."));
    assert(!strstr(captured,"Command GP1"));
    snapshot.intel.vendor=0x8086; snapshot.intel.error=IL_TIMEOUT;
    snapshot.intel.operation_error=IL_TIMEOUT; snapshot.intel.stop_failures=2;
    snapshot.intel.init_alive=snapshot.intel.runtime_alive=1; snapshot.intel.boot_stage=7;
    snapshot.intel.link_power=(il3945_link_power_t){0x103,0,0x143,0x40,1,1};
    snapshot.intel.last_command=0x77; snapshot.intel.command_pending=snapshot.intel.command_poisoned=1;
    snapshot.intel.command_gp1=4; snapshot.intel.command_tx_config=0x80200008;
    snapshot.intel.command_tx_status=0x10100000;
    snapshot.intel.command_gp=1; snapshot.intel.command_fh=0x10000;
    snapshot.intel.command_tx_base=0x200000;
    snapshot.intel.command_scd_mode=2; snapshot.intel.command_scd_fifos=0x3f;
    snapshot.intel.command_rx_read=2; snapshot.intel.command_rx_closed=3;
    captured[0]=0; shell_wifi_status(output);
    assert(strstr(captured,"Error: -3"));
    assert(strstr(captured,"Original operation error: -3"));
    assert(strstr(captured,"Stop failures: TX"));
    assert(strstr(captured,"init=1 runtime=1"));
    assert(strstr(captured,"Boot stage: 7"));
    assert(strstr(captured,"PCIe endpoint link before/after: 0x0103/0x0000"));
    assert(strstr(captured,"PCIe parent link before/after: 0x0143/0x0040"));
    assert(strstr(captured,"0x0077 pending=1 poisoned=1"));
    assert(strstr(captured,"Command GP1: 0x00000004"));
    assert(strstr(captured,"TX queue 4 config: 0x80200008"));
    assert(strstr(captured,"TX hardware status: 0x10100000"));
    assert(strstr(captured,"Flow-handler IRQ status: 0x00010000"));
    assert(strstr(captured,"TX shared base: 0x00200000"));
    assert(strstr(captured,"Scheduler mode: 0x00000002"));
    assert(strstr(captured,"Scheduler active FIFOs: 0x0000003F"));
    assert(strstr(captured,"RX read/closed: 2/3"));
    snapshot.intel.last_command=0x18;
    snapshot.intel.error=IL_VERIFY;
    snapshot.intel.command_pending=snapshot.intel.command_poisoned=0;
    snapshot.intel.command_response_id=0x18;
    snapshot.intel.command_response_length=4;
    snapshot.intel.command_response_status=2;
    captured[0]=0; shell_wifi_status(output);
    assert(strstr(captured,"Command reply ID/flags: 0x0018/0x0000"));
    assert(strstr(captured,"Command reply bytes: 4"));
    assert(strstr(captured,"Command reply status: 0x00000002"));
    snapshot.intel.last_command=0x80;
    snapshot.intel.scan_completion_length=12;
    snapshot.intel.scan_completed_channels=12;
    snapshot.intel.scan_expected_channels=13;
    captured[0]=0; shell_wifi_status(output);
    assert(strstr(captured,"Scan completion bytes: 12"));
    assert(strstr(captured,"Scan channels completed/expected: 12/13"));
    assert(strstr(captured,"Scan completion status: 0x0000"));
    snapshot.intel.error_reply.valid=1;
    snapshot.intel.error_reply.type=5;
    snapshot.intel.error_reply.command=0x1c;
    snapshot.intel.error_reply.sequence=7;
    snapshot.intel.error_reply.info=0x00740000;
    snapshot.intel.last_data_tx.valid=1;
    snapshot.intel.last_data_tx.sequence=7;
    snapshot.intel.last_data_tx.frame_length=100;
    snapshot.intel.last_data_tx.first_length=80;
    snapshot.intel.last_data_tx.payload_length=76;
    snapshot.intel.firmware_error.count=1;
    snapshot.intel.firmware_error.entry[0]=5;
    snapshot.intel.firmware_error.entry[2]=0x1111;
    snapshot.intel.firmware_error.entry[6]=77;
    snapshot.intel.runtime_error=IL_VERIFY;
    snapshot.intel.runtime_irq=0x02000000;
    snapshot.intel.data_tx_pending=1;
    snapshot.intel.data_tx_poisoned=0;
    captured[0]=0; shell_wifi_status(output);
    assert(strstr(captured,"Runtime IRQ/FH status: 0x02000000/0x00000000"));
    assert(strstr(captured,"Data TX pending/poisoned: 1/0"));
    assert(strstr(captured,"Firmware error code: 0x00000005"));
    assert(strstr(captured,"Firmware assert PC/line: 0x00001111/77"));
    assert(strstr(captured,"Firmware bad command/sequence: 0x001C/0x0007"));
    assert(strstr(captured,"Firmware error type/info: 0x00000005/0x00740000"));
    assert(strstr(captured,"Last data TX sequence/length: 0x0007/100"));
    assert(strstr(captured,"Data TFD control/first/payload: 0x00000000/80/76"));
    assert(strstr(captured,"Diagnostics format: 5"));
    snapshot.intel.error_reply.valid=0; snapshot.intel.last_data_tx.valid=0;
    captured[0]=0; shell_wifi_status(output);
    assert(strstr(captured,"Firmware bad command/sequence: not captured"));
    assert(strstr(captured,"Firmware error type/info: not captured"));
    assert(strstr(captured,"Last data TX sequence/length: not captured"));
    assert(strstr(captured,"Data TX flags/rate/station/security: not captured"));
    assert(strstr(captured,"Data TFD control/first/payload: not captured"));
    snapshot.intel.runtime_error=0; snapshot.intel.state=IL_CONNECTION_FAILED;
    captured[0]=0; shell_wifi_status(output);
    assert(strstr(captured,"Runtime snapshot: not captured (connection setup failure)"));
    assert(strstr(captured,"Last data TX sequence/length: not captured"));
    snapshot.intel.security_phase=2; snapshot.intel.port_authorized=snapshot.intel.port_active=1;
    snapshot.intel.tx_history_count=2;
    snapshot.intel.tx_history[0]=(il3945_tx_snapshot_t){.sequence=0x206,.frame_length=76,.ether_type=0x806,.completed=1,.status=1};
    snapshot.intel.tx_history[1]=(il3945_tx_snapshot_t){.sequence=0x207,.frame_length=76,.ether_type=0x806};
    snapshot.intel.connection_phase=3;
    snapshot.intel.connection_reject=IL_REJECT_ASSOC_IES;
    snapshot.intel.management_status=0;
    snapshot.intel.association_capability=0x431;
    snapshot.intel.association_aid=0xf801;
    captured[0]=0; shell_wifi_status(output);
    assert(strstr(captured,"Connection failure phase/reason: 3/association IEs"));
    assert(strstr(captured,"AP management status: 0"));
    assert(strstr(captured,"WPA phase/authorized/port-open: 2/1/1"));
    assert(strstr(captured,"TX history seq/len/type/status: 0x0206/76/0x0806/0x00000001"));
    assert(strstr(captured,"TX history seq/len/type/status: 0x0207/76/0x0806/pending"));
    assert(strstr(captured,"Association capability/raw AID: 0x0431/0xF801"));
    shell_wifi_status(0);
    puts("Wi-Fi status: absent adapter and complete command-failure snapshot passed");
}

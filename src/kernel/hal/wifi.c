#include "wifi.h"
#include "../mutex.h"
static wifi_status_t status;
static il_scan_results_t scan_results;
static uint8_t scan_valid, scan_busy;
void sys_wifi_probe(void) {
    MUTEX_GUARD(kernel_mutex);
    if (scan_busy) return;
    scan_valid = 0; scan_results = (il_scan_results_t){0};
    b43_probe(&status.adapter);
    iwlegacy_probe(&status.intel);
    status.generation++;
}
void sys_wifi_get_status(wifi_status_t* out) {
    MUTEX_GUARD(kernel_mutex);
    if (out) *out = status;
}
const char* sys_wifi_status_text(void) {
    MUTEX_GUARD(kernel_mutex);
    if (status.intel.vendor) {
        switch (status.intel.state) {
        case IL_SCAN_COMPLETE: return "Wi-Fi scan complete.";
        case IL_SCAN_FAILED: return "Wi-Fi scan failed.";
        case IL_FIRMWARE_READY: return "Wi-Fi firmware verified; scanning is not available yet.";
        case IL_FIRMWARE_MISSING: return "Wi-Fi firmware is missing.";
        case IL_FIRMWARE_FAILED:
            return status.intel.error == IL_RFKILL ? "Wi-Fi is blocked by the hardware switch." :
                "Wi-Fi firmware initialization failed.";
        case IL_EEPROM_READY: return "Wi-Fi adapter found; connection support unavailable.";
        case IL_DEVICE_BUSY: return "Wi-Fi adapter is already active.";
        case IL_CLOCK_TIMEOUT:
        case IL_EEPROM_FAILED:
        case IL_ACCESS_FAILED: return "Unable to initialize Wi-Fi adapter.";
        default: return "Wi-Fi adapter found; connection support unavailable.";
        }
    }
    switch (status.adapter.state) {
    case B43_BUS_UNSUPPORTED:
    case B43_LEGACY: return "Wi-Fi adapter not supported yet.";
    case B43_PROBE_FAILED: return "Unable to access Wi-Fi adapter.";
    case B43_CORE_FOUND: return "Wi-Fi adapter found; radio support unavailable.";
    default: return "No Wi-Fi adapter found.";
    }
}
const char* sys_wifi_scan(void) {
    iwlegacy_info_t adapter;
    {
        MUTEX_GUARD(kernel_mutex);
        if (scan_busy) return "Wi-Fi operation in progress.";
        if (!status.intel.eeprom.version)
            return status.adapter.state == B43_CORE_FOUND ? "Network scanning is not available yet." : sys_wifi_status_text();
        adapter = status.intel; scan_busy = 1; scan_valid = 0;
    }
    /* Long polled firmware operations must not hold the window/network lock.
     * Publish a complete snapshot only after the operation has finished. */
    il_scan_results_t found;
    int result = iwlegacy_scan(&adapter,&found);
    {
        MUTEX_GUARD(kernel_mutex);
        status.intel = adapter; status.generation++; scan_busy = 0;
        if (!result) { scan_results = found; scan_valid = 1; }
    }
    if (result) return sys_wifi_status_text();
    return found.count ? "Wi-Fi networks found." : "No Wi-Fi networks found.";
}

unsigned sys_wifi_network_count(void) {
    MUTEX_GUARD(kernel_mutex);
    return scan_valid ? scan_results.count : 0;
}
int sys_wifi_get_network(unsigned index, il_network_t* out) {
    MUTEX_GUARD(kernel_mutex);
    if (!out || !scan_valid || index >= scan_results.count) return 0;
    *out = scan_results.networks[index]; return 1;
}

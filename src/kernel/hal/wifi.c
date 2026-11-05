#include "wifi.h"
#include "../mutex.h"
static wifi_status_t status;
void sys_wifi_probe(void) {
    MUTEX_GUARD(kernel_mutex);
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
    MUTEX_GUARD(kernel_mutex);
    if (status.intel.eeprom.version) {
        (void)iwlegacy_firmware_check(&status.intel);
        status.generation++;
        return sys_wifi_status_text();
    }
    if (status.adapter.state != B43_CORE_FOUND && status.intel.state != IL_EEPROM_READY)
        return sys_wifi_status_text();
    return "Network scanning is not available yet.";
}

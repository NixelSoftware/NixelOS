#include "wifi.h"
#include "../mutex.h"
static wifi_status_t status;
void sys_wifi_probe(void) {
    MUTEX_GUARD(kernel_mutex);
    b43_probe(&status.adapter);
    status.generation++;
}
void sys_wifi_get_status(wifi_status_t* out) {
    MUTEX_GUARD(kernel_mutex);
    if (out) *out = status;
}
const char* sys_wifi_status_text(void) {
    MUTEX_GUARD(kernel_mutex);
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
    if (status.adapter.state != B43_CORE_FOUND) return sys_wifi_status_text();
    return "Network scanning is not available yet.";
}

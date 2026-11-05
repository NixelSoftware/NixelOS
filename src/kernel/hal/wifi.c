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
    return b43_probe_description(status.adapter.state);
}
const char* sys_wifi_scan(void) {
    MUTEX_GUARD(kernel_mutex);
    if (status.adapter.state != B43_CORE_FOUND) return sys_wifi_status_text();
    return "Scan unavailable: PHY and packet transport not ported.";
}

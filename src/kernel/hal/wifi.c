#include "wifi.h"
#include "../mutex.h"
#include "net.h"
#include "../net/wireless/security/wpa2.h"
#include "../scheduler.h"
#include "../crypto/entropy.h"
#include "../net/wireless/intel/iwlegacy/station.h"
static wifi_status_t status;
static il_scan_results_t scan_results;
static uint8_t scan_valid, scan_busy;
void sys_wifi_probe(void) {
    MUTEX_GUARD(kernel_mutex);
    if (scan_busy || iwlegacy_connected()) return;
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
        case IL_CONNECTED: return "Wi-Fi connected.";
        case IL_CONNECTION_FAILED: return status.intel.error==IL_RFKILL ?
            "Wi-Fi is blocked by the hardware switch." : "Wi-Fi connection failed.";
        case IL_SCAN_COMPLETE: return "Wi-Fi scan complete.";
        case IL_SCAN_FAILED:
            if (status.intel.error == IL_TIMEOUT) {
                if (status.intel.last_command == 0x10 && status.intel.command_pending)
                    return "Wi-Fi RXON command timed out.";
                if (status.intel.last_command == 0x80 && !status.intel.command_pending)
                    return "Wi-Fi scan completion timed out.";
                return "Wi-Fi firmware command timed out.";
            }
            return "Wi-Fi scan failed.";
        case IL_FIRMWARE_READY: return "Wi-Fi firmware ready.";
        case IL_FIRMWARE_MISSING: return "Wi-Fi firmware is missing.";
        case IL_FIRMWARE_FAILED:
            if (status.intel.error == IL_DMA_STOP) return "Wi-Fi DMA stop unconfirmed; reboot required.";
            return status.intel.error == IL_RFKILL ? "Wi-Fi is blocked by the hardware switch." :
                "Wi-Fi firmware initialization failed.";
        case IL_EEPROM_READY: return "Wi-Fi adapter ready; scan to select a network.";
        case IL_DEVICE_BUSY: return "Wi-Fi adapter is already active.";
        case IL_CLOCK_TIMEOUT: return "Wi-Fi initialization: MAC clock timeout.";
        case IL_EEPROM_FAILED:
            return status.intel.error == IL_TIMEOUT ? "Wi-Fi initialization: EEPROM read timeout." :
                "Wi-Fi initialization: invalid or inaccessible EEPROM.";
        case IL_ACCESS_FAILED:
            return status.intel.error == IL_TIMEOUT ? "Wi-Fi initialization: reset timeout." :
                "Wi-Fi initialization: PCI/register access failed.";
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
        if (iwlegacy_connected()) return "Disconnect Wi-Fi before scanning.";
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

const char* sys_wifi_connect(unsigned index,const uint8_t* password,unsigned length) {
    iwlegacy_info_t adapter; il_network_t ap;
    {
        MUTEX_GUARD(kernel_mutex);
        if(scan_busy) return "Wi-Fi operation in progress.";
        if(iwlegacy_connected()) return "Disconnect before selecting another network.";
        if(!scan_valid || index>=scan_results.count) return "Scan and select a network first.";
        ap=scan_results.networks[index]; adapter=status.intel;
        if(ap.security==IL_SECURITY_UNSUPPORTED) return "This network security is not supported.";
        scan_busy=1;
    }
    /* Entropy startup may collect timing samples; never hold the shared
     * window/network mutex while it runs. Reserve the operation first. */
    if(ap.security==IL_SECURITY_WPA2_PSK) {
        uint8_t probe[32]; int r=kernel_entropy(0,probe,sizeof(probe));
        wifi_crypto_erase(probe,sizeof(probe));
        if(r) {
            MUTEX_GUARD(kernel_mutex); scan_busy=0;
            return "WPA2 entropy health check failed; retry connecting.";
        }
    }
    int result=iwlegacy_connect(&adapter,&ap,password,length,kernel_entropy,0);
    {
        MUTEX_GUARD(kernel_mutex);
        status.intel=adapter; status.generation++; scan_busy=0;
        if(!result) sys_net_select_wifi();
    }
    if(result) return result==IL_RFKILL ? "Wi-Fi is blocked by the hardware switch." : "Wi-Fi connection failed.";
    sys_net_configure_address();
    return "Wi-Fi connected.";
}
void sys_wifi_disconnect(void) {
    MUTEX_GUARD(kernel_mutex);
    if(scan_busy) return;
    int r=iwlegacy_disconnect();
    status.intel.state=r ? IL_FIRMWARE_FAILED : IL_EEPROM_READY;
    status.intel.error=r; status.generation++;
}
int sys_wifi_is_connected(void) {
    MUTEX_GUARD(kernel_mutex);
    return !scan_busy && iwlegacy_connected();
}
void sys_wifi_poll(void) {
    MUTEX_GUARD(kernel_mutex);
    if(scan_busy || !iwlegacy_connected()) return;
    /* Divide the monotonic ns clock without a 64-bit libgcc dependency. */
    uint64_t ns=scheduler_clock(); unsigned ms=0, remainder=0;
    for(int bit=63;bit>=0;bit--) {
        remainder=(remainder<<1)|(unsigned)((ns>>bit)&1);
        if(remainder>=1000000u) { remainder-=1000000u; if(bit<32) ms|=1u<<bit; }
    }
    int r=iwlegacy_poll(ms);
    if(r) {
        iwlegacy_get_runtime_status(&status.intel);
        /* Drop stale addressing immediately when the authorized port closes. */
        sys_net_shutdown();
        status.intel.state=IL_CONNECTION_FAILED; status.intel.error=r; status.generation++;
    }
}

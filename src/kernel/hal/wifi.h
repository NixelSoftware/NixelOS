#ifndef NIXEL_HAL_WIFI_H
#define NIXEL_HAL_WIFI_H
#include "../net/wireless/broadcom/b43/b43.h"
#include "../net/wireless/intel/iwlegacy/iwlegacy.h"
#include "../net/wireless/intel/iwlegacy/scan.h"
typedef struct {
    b43_info_t adapter;
    iwlegacy_info_t intel;
    uint32_t generation;
} wifi_status_t;
/* Independent of the selected Ethernet adapter. No simulated scan results. */
void sys_wifi_probe(void);
void sys_wifi_get_status(wifi_status_t* out);
const char* sys_wifi_status_text(void);
unsigned sys_wifi_network_count(void);
int sys_wifi_get_network(unsigned index, il_network_t* out);
/* Passive scan of EEPROM-permitted channels on supported Intel hardware. */
const char* sys_wifi_scan(void);
#endif

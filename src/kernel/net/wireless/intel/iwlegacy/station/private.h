/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NIXEL_IL3945_STATION_PRIVATE_H
#define NIXEL_IL3945_STATION_PRIVATE_H
#include "../internal.h"
#include "../station.h"
#include "../bss.h"
#include "../../../ieee80211.h"
#include "../../../security/wpa2.h"
static inline void copy(uint8_t* d,const uint8_t* s,unsigned n) { while(n--) *d++=*s++; }
static inline int same(const uint8_t* a,const uint8_t* b,unsigned n) { while(n--) if(*a++!=*b++) return 0; return 1; }
static inline void put16(uint8_t* p,unsigned v) { p[0]=v; p[1]=v>>8; }
static inline void put32(uint8_t* p,uint32_t v) { put16(p,v); put16(p+2,v>>16); }
void il_station_firmware_error(void*,uint8_t,uint8_t,uint16_t,const uint8_t*,unsigned);
void il_station_event(void*,uint8_t,uint8_t,uint16_t,const uint8_t*,unsigned);
int il_station_pump(il_station_t*);
int il_station_wait_tx(il_station_t*);
int il_station_command(il_station_t*,uint8_t,const void*,unsigned);
int il_station_power(il_station_t*);
int il_station_rxon(il_station_t*,unsigned);
int il_station_transmit(il_station_t*,const uint8_t*,unsigned,uint16_t ether_type);
unsigned il_station_management(il_station_t*,unsigned);
int il_station_process(il_station_t*);
int il_station_rates(il_station_t*,const il_network_t*);
int il_station_start_radio(il_station_t*);
int il_station_management_rx(il_station_t*,const uint8_t*,unsigned);
int il_station_monitor(il_station_t*);
int il_station_duplicate(il_station_t*,const uint8_t*,unsigned,int commit);
int il_station_update_bss(il_station_t*);
void il_station_capture_failure(il_station_t*,int);
#endif

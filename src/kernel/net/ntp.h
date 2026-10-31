#ifndef NTP_H
#define NTP_H
#include "../drivers/rtc.h"
void time_init(void);
void ntp_poll(void);
int time_configure(int enabled, const char* server, int offset_minutes);
int time_sync_enabled(void);
const char* time_server(void);
int time_zone_offset(void);
const char* time_sync_status(void);
void time_sync_now(void);
int time_set_local(const rtc_time_t* time);
uint64_t time_to_epoch(const rtc_time_t* time);
void time_from_epoch(uint64_t seconds, rtc_time_t* time);
void rtc_set_epoch(uint64_t seconds);
#endif

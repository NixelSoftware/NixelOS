#ifndef KERNEL_DRIVERS_RTC_H
#define KERNEL_DRIVERS_RTC_H

#include "../types.h"

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} rtc_time_t;

void rtc_get_time(rtc_time_t* out);
void rtc_format_time_hm(char out[6]); /* HH:MM */

#endif
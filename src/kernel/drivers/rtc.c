#include "rtc.h"

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

static inline uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDRESS, (inb(CMOS_ADDRESS) & 0x80) | (reg & 0x7F));
    return inb(CMOS_DATA);
}

static inline int cmos_is_updating(void) {
    outb(CMOS_ADDRESS, 0x0A);
    return (inb(CMOS_DATA) & 0x80);
}

static uint8_t bcd_to_bin(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

void rtc_get_time(rtc_time_t* out) {
    if (!out) return;

    while (cmos_is_updating()) {
        __asm__ volatile ("pause");
    }

    uint8_t sec   = cmos_read(0x00);
    uint8_t min   = cmos_read(0x02);
    uint8_t hour  = cmos_read(0x04);
    uint8_t day   = cmos_read(0x07);
    uint8_t month = cmos_read(0x08);
    uint16_t year = cmos_read(0x09);
    uint8_t reg_b = cmos_read(0x0B);

    if (!(reg_b & 0x04)) {
        sec   = bcd_to_bin(sec);
        min   = bcd_to_bin(min);
        hour  = ((hour & 0x7F) ? bcd_to_bin(hour & 0x7F) : 0) | (hour & 0x80);
        day   = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year  = bcd_to_bin((uint8_t)year);
    }

    if (!(reg_b & 0x02) && (hour & 0x80)) {
        hour = ((hour & 0x7F) + 12) % 24;
    }

    out->second = sec;
    out->minute = min;
    out->hour   = hour;
    out->day    = day;
    out->month  = month;
    out->year   = year + 2000;
}

void rtc_format_time_hm(char out[6]) {
    rtc_time_t t;
    rtc_get_time(&t);

    out[0] = '0' + (t.hour / 10);
    out[1] = '0' + (t.hour % 10);
    out[2] = ':';
    out[3] = '0' + (t.minute / 10);
    out[4] = '0' + (t.minute % 10);
    out[5] = '\0';
}
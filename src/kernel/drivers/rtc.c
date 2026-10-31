#include "../net/ntp.h"
#include "../task.h"
#include "../mutex.h"
#include "rtc.h"
#include "acpi.h"
#include "../startup_trace.h"

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

static inline uint8_t cmos_read(uint8_t reg) {
    uint64_t began=startup_trace_now();
    outb(CMOS_ADDRESS, (inb(CMOS_ADDRESS) & 0x80) | (reg & 0x7F));
    uint8_t value=inb(CMOS_DATA);
    startup_trace_span(TR_RTC_IO,began,reg,value);
    return value;
}

static inline int cmos_is_updating(void) {
    uint64_t began=startup_trace_now();
    outb(CMOS_ADDRESS, (inb(CMOS_ADDRESS) & 0x80) | 0x0A);
    uint8_t value=inb(CMOS_DATA);
    startup_trace_span(TR_RTC_IO,began,0x0a,value);
    return value & 0x80;
}

static uint8_t bcd_to_bin(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

static void rtc_read_hardware(rtc_time_t* out) {
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
    uint8_t century_reg = acpi_rtc_century_register();
    uint8_t century = century_reg ? cmos_read(century_reg) : 0;

    if (!(reg_b & 0x04)) {
        sec   = bcd_to_bin(sec);
        min   = bcd_to_bin(min);
        hour  = ((hour & 0x7F) ? bcd_to_bin(hour & 0x7F) : 0) | (hour & 0x80);
        day   = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year  = bcd_to_bin((uint8_t)year);
        century = bcd_to_bin(century);
    }

    if (!(reg_b & 0x02)) {
        hour = ((hour & 0x7F) % 12) + ((hour & 0x80) ? 12 : 0);
    }

    out->second = sec;
    out->minute = min;
    out->hour   = hour;
    out->day    = day;
    out->month  = month;
    out->year   = year + ((century == 19 || century == 20) ? century * 100 : (year >= 70 ? 1900 : 2000));
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
static void cmos_write(uint8_t reg, uint8_t value) {
    outb(CMOS_ADDRESS, (inb(CMOS_ADDRESS) & 0x80) | (reg & 0x7f));
    outb(CMOS_DATA, value);
}
static uint8_t rtc_encode(uint8_t value, uint8_t mode) {
    return (mode & 0x04) ? value : (uint8_t)(((value / 10) << 4) | (value % 10));
}
static void rtc_write_hardware(const rtc_time_t* utc, uint64_t seconds) {
    /* Keep CMOS address/data transactions and the frozen update atomic. */
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) :: "memory");
    uint8_t address = inb(CMOS_ADDRESS);
    uint8_t mode = cmos_read(0x0b);
    cmos_write(0x0b, mode | 0x80); /* SET inhibits calendar updates. */
    uint8_t hour = utc->hour;
    uint8_t pm = 0;
    if (!(mode & 0x02)) {
        pm = hour >= 12 ? 0x80 : 0;
        hour %= 12;
        if (!hour) hour = 12;
    }
    cmos_write(0x00, rtc_encode(utc->second, mode));
    cmos_write(0x02, rtc_encode(utc->minute, mode));
    cmos_write(0x04, rtc_encode(hour, mode) | pm);
    cmos_write(0x06, rtc_encode(((uint32_t)seconds / 86400 + 4) % 7 + 1, mode));
    cmos_write(0x07, rtc_encode(utc->day, mode));
    cmos_write(0x08, rtc_encode(utc->month, mode));
    cmos_write(0x09, rtc_encode(utc->year % 100, mode));
    uint8_t century = acpi_rtc_century_register();
    if (century) cmos_write(century, rtc_encode(utc->year / 100, mode));
    cmos_write(0x0b, mode);
    outb(CMOS_ADDRESS, address);
    __asm__ volatile ("pushl %0; popfl" :: "r"(flags) : "memory", "cc");
}
static uint64_t base_epoch, base_tick;
static int clock_initialized;
void rtc_set_epoch(uint64_t seconds) {
    MUTEX_GUARD(kernel_mutex);
    rtc_time_t utc;
    time_from_epoch(seconds, &utc);
    rtc_write_hardware(&utc, seconds);
    base_epoch = seconds; base_tick = scheduler_clock(); clock_initialized = 1;
}
static uint64_t elapsed_seconds(uint64_t ns) {
    uint64_t quotient = 0, remainder = 0;
    for (int i = 63; i >= 0; i--) {
        remainder = (remainder << 1) | ((ns >> i) & 1);
        if (remainder >= 1000000000u) { remainder -= 1000000000u; quotient |= 1ull << i; }
    }
    return quotient;
}
void rtc_get_time(rtc_time_t* out) {
    MUTEX_GUARD(kernel_mutex);
    if (!out) return;
    if (!clock_initialized) {
        rtc_time_t hardware; rtc_read_hardware(&hardware);
        if (hardware.month < 1 || hardware.month > 12 || hardware.day < 1 || hardware.day > 31)
            hardware = (rtc_time_t){0,0,0,1,1,2026};
        base_epoch = time_to_epoch(&hardware);
        base_tick = scheduler_clock();
        clock_initialized = 1;
    }
    int64_t local = (int64_t)(base_epoch + elapsed_seconds(scheduler_clock() - base_tick)) + time_zone_offset() * 60;
    time_from_epoch(local < 0 ? 0 : local, out);
}

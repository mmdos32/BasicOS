/* rtc.h - CMOS real-time clock. */
#ifndef BASIC_RTC_H
#define BASIC_RTC_H

#include <stdint.h>

typedef struct { uint8_t sec, min, hour, day, mon; uint16_t year; } rtc_t;

void rtc_read(rtc_t *t);

#endif

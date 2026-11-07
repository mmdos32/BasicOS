/* rtc.c - CMOS real-time clock (BCD + 12-hour mode handled). */
#include <stdint.h>

#include "idt.h"
#include "io.h"
#include "rtc.h"

static uint8_t cmos(uint8_t reg) { outb(0x70, reg); return inb(0x71); }

#define BCD(x) ((uint8_t)(((x) & 0x0F) + ((x) >> 4) * 10))

void rtc_read(rtc_t *t) {
    /* the index/data ports are shared, so no task switch may happen in between */
    uint32_t fl = irq_save();
    for (int i = 0; i < 100000 && (cmos(0x0A) & 0x80); i++) ;
    uint8_t s = cmos(0x00), m = cmos(0x02), h = cmos(0x04);
    uint8_t d = cmos(0x07), mo = cmos(0x08), y = cmos(0x09), b = cmos(0x0B);
    irq_restore(fl);

    int pm = h & 0x80;
    h &= 0x7F;
    if (!(b & 4)) { s = BCD(s); m = BCD(m); h = BCD(h); d = BCD(d); mo = BCD(mo); y = BCD(y); }
    if (!(b & 2)) h = pm ? (h % 12) + 12 : h % 12;      /* 12-hour mode */
    t->sec = s; t->min = m; t->hour = h; t->day = d; t->mon = mo; t->year = 2000 + y;
}

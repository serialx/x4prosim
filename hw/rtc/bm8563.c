/*
 * BM8563 I2C real-time clock (PCF8563 register set)
 *
 * Registers 0x00-0x0F with the PCF8563 auto-incrementing pointer. The time
 * registers 0x02-0x08 (VL_seconds .. years, century bit in months) follow the
 * host clock plus an offset that guest writes move; the time is latched on
 * every START so a burst read is coherent. VL starts clear so the time reads
 * as valid. Control, alarm, CLKOUT and timer registers are plain storage: no
 * alarm/timer interrupts, no STOP bit, no CLKOUT. Battery-backed, so an SoC
 * reset keeps the time and registers.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/bcd.h"
#include "qemu/module.h"
#include "hw/i2c/i2c.h"
#include "system/rtc.h"

#define TYPE_BM8563 "bm8563"
OBJECT_DECLARE_SIMPLE_TYPE(BM8563State, BM8563)

#define BM8563_NREGS        16
#define BM8563_SECONDS      0x02
#define BM8563_WEEKDAYS     0x06
#define BM8563_MONTHS       0x07
#define BM8563_YEARS        0x08
#define BM8563_VL           0x80
#define BM8563_CENTURY      0x80    /* set = 19xx */

struct BM8563State {
    I2CSlave parent_obj;

    time_t offset;
    uint8_t wday_offset;
    uint8_t regs[BM8563_NREGS];
    uint8_t ptr;
    bool ptr_next;      /* next byte written is the register pointer */
    bool time_written;
};

static void bm8563_latch_time(BM8563State *s)
{
    struct tm now;
    int year;

    qemu_get_timedate(&now, s->offset);
    year = now.tm_year + 1900;
    s->regs[BM8563_SECONDS] = (s->regs[BM8563_SECONDS] & BM8563_VL) | to_bcd(now.tm_sec);
    s->regs[0x03] = to_bcd(now.tm_min);
    s->regs[0x04] = to_bcd(now.tm_hour);
    s->regs[0x05] = to_bcd(now.tm_mday);
    s->regs[BM8563_WEEKDAYS] = (now.tm_wday + s->wday_offset) % 7;
    s->regs[BM8563_MONTHS] = (year < 2000 ? BM8563_CENTURY : 0) | to_bcd(now.tm_mon + 1);
    s->regs[BM8563_YEARS] = to_bcd(year % 100);
}

static void bm8563_commit_time(BM8563State *s)
{
    struct tm tm = {
        .tm_sec = from_bcd(s->regs[BM8563_SECONDS] & 0x7f),
        .tm_min = from_bcd(s->regs[0x03] & 0x7f),
        .tm_hour = from_bcd(s->regs[0x04] & 0x3f),
        .tm_mday = from_bcd(s->regs[0x05] & 0x3f),
        .tm_mon = from_bcd(s->regs[BM8563_MONTHS] & 0x1f) - 1,
        .tm_year = from_bcd(s->regs[BM8563_YEARS]) +
                   (s->regs[BM8563_MONTHS] & BM8563_CENTURY ? 0 : 100),
    };
    struct tm now;

    s->offset = qemu_timedate_diff(&tm);
    qemu_get_timedate(&now, s->offset);
    s->wday_offset = ((s->regs[BM8563_WEEKDAYS] & 7) + 7 - now.tm_wday) % 7;
}

static int bm8563_event(I2CSlave *i2c, enum i2c_event event)
{
    BM8563State *s = BM8563(i2c);

    switch (event) {
    case I2C_START_SEND:
        s->ptr_next = true;
        /* fall through */
    case I2C_START_RECV:
        bm8563_latch_time(s);
        break;
    case I2C_FINISH:
        if (s->time_written) {
            s->time_written = false;
            bm8563_commit_time(s);
        }
        break;
    default:
        break;
    }
    return 0;
}

static uint8_t bm8563_recv(I2CSlave *i2c)
{
    BM8563State *s = BM8563(i2c);
    uint8_t val = s->regs[s->ptr];

    s->ptr = (s->ptr + 1) % BM8563_NREGS;
    return val;
}

static int bm8563_send(I2CSlave *i2c, uint8_t data)
{
    BM8563State *s = BM8563(i2c);

    if (s->ptr_next) {
        s->ptr_next = false;
        s->ptr = data % BM8563_NREGS;
        return 0;
    }
    s->regs[s->ptr] = data;
    if (s->ptr >= BM8563_SECONDS && s->ptr <= BM8563_YEARS) {
        s->time_written = true;
    }
    s->ptr = (s->ptr + 1) % BM8563_NREGS;
    return 0;
}

static void bm8563_class_init(ObjectClass *klass, const void *data)
{
    I2CSlaveClass *sc = I2C_SLAVE_CLASS(klass);

    sc->event = bm8563_event;
    sc->recv = bm8563_recv;
    sc->send = bm8563_send;
}

static const TypeInfo bm8563_info = {
    .name = TYPE_BM8563,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(BM8563State),
    .class_init = bm8563_class_init,
};

static void bm8563_register_types(void)
{
    type_register_static(&bm8563_info);
}

type_init(bm8563_register_types)

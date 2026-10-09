/*
 * DS3231 I2C real-time clock.
 *
 * Registers 0x00-0x12 with the auto-incrementing pointer. The time registers
 * 0x00-0x06 (seconds .. year, 24-hour mode, weekday 1-7 with Sunday = 7)
 * follow the host clock plus an offset that guest writes move; the time is
 * latched on every START so a burst read is coherent. Control (0x0E, reset
 * 0x1C) and Status (0x0F, OSF clear so the time reads as valid) are stored;
 * alarms, the square wave and the aging offset are plain storage; the
 * temperature reads 25 C. Battery-backed: an SoC reset keeps everything.
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

#define TYPE_DS3231 "ds3231"
OBJECT_DECLARE_SIMPLE_TYPE(DS3231State, DS3231)

#define DS3231_NREGS    0x13
#define R_SECONDS       0x00
#define R_MINUTES       0x01
#define R_HOURS         0x02
#define R_DAY           0x03
#define R_DATE          0x04
#define R_MONTH         0x05
#define R_YEAR          0x06
#define R_CONTROL       0x0E
#define R_STATUS        0x0F
#define R_TEMP_MSB      0x11
#define HOURS_12H       0x40
#define HOURS_PM        0x20
#define MONTH_CENTURY   0x80

struct DS3231State {
    I2CSlave parent_obj;

    time_t offset;
    uint8_t wday_offset;
    uint8_t regs[DS3231_NREGS];
    uint8_t ptr;
    bool ptr_next;
    bool time_written;
};

static void ds3231_latch_time(DS3231State *s)
{
    struct tm now;

    qemu_get_timedate(&now, s->offset);
    s->regs[R_SECONDS] = to_bcd(now.tm_sec);
    s->regs[R_MINUTES] = to_bcd(now.tm_min);
    s->regs[R_HOURS] = to_bcd(now.tm_hour);
    s->regs[R_DAY] = (now.tm_wday + s->wday_offset) % 7 ?: 7;
    s->regs[R_DATE] = to_bcd(now.tm_mday);
    s->regs[R_MONTH] = to_bcd(now.tm_mon + 1) | (now.tm_year >= 200 ? MONTH_CENTURY : 0);
    s->regs[R_YEAR] = to_bcd((now.tm_year + 1900) % 100);
}

static void ds3231_commit_time(DS3231State *s)
{
    uint8_t h = s->regs[R_HOURS];
    int hour = h & HOURS_12H ? from_bcd(h & 0x1f) % 12 + (h & HOURS_PM ? 12 : 0)
                             : from_bcd(h & 0x3f);
    struct tm tm = {
        .tm_sec = from_bcd(s->regs[R_SECONDS] & 0x7f),
        .tm_min = from_bcd(s->regs[R_MINUTES] & 0x7f),
        .tm_hour = hour,
        .tm_mday = from_bcd(s->regs[R_DATE] & 0x3f),
        .tm_mon = from_bcd(s->regs[R_MONTH] & 0x1f) - 1,
        .tm_year = from_bcd(s->regs[R_YEAR]) + (s->regs[R_MONTH] & MONTH_CENTURY ? 200 : 100),
    };
    struct tm now;

    s->offset = qemu_timedate_diff(&tm);
    qemu_get_timedate(&now, s->offset);
    s->wday_offset = ((s->regs[R_DAY] & 7) % 7 + 7 - now.tm_wday) % 7;
}

static int ds3231_event(I2CSlave *i2c, enum i2c_event event)
{
    DS3231State *s = DS3231(i2c);

    switch (event) {
    case I2C_START_SEND:
        s->ptr_next = true;
        /* fall through */
    case I2C_START_RECV:
        ds3231_latch_time(s);
        break;
    case I2C_FINISH:
        if (s->time_written) {
            s->time_written = false;
            ds3231_commit_time(s);
        }
        break;
    default:
        break;
    }
    return 0;
}

static uint8_t ds3231_recv(I2CSlave *i2c)
{
    DS3231State *s = DS3231(i2c);
    uint8_t val = s->regs[s->ptr];

    s->ptr = (s->ptr + 1) % DS3231_NREGS;
    return val;
}

static int ds3231_send(I2CSlave *i2c, uint8_t data)
{
    DS3231State *s = DS3231(i2c);

    if (s->ptr_next) {
        s->ptr_next = false;
        s->ptr = data % DS3231_NREGS;
        return 0;
    }
    if (s->ptr <= R_YEAR) {
        s->time_written = true;
    }
    if (s->ptr != R_TEMP_MSB && s->ptr != R_TEMP_MSB + 1) {
        s->regs[s->ptr] = data;
    }
    s->ptr = (s->ptr + 1) % DS3231_NREGS;
    return 0;
}

static void ds3231_realize(DeviceState *dev, Error **errp)
{
    DS3231State *s = DS3231(dev);

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[R_CONTROL] = 0x1C;
    s->regs[R_TEMP_MSB] = 25;
}

static void ds3231_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->realize = ds3231_realize;
    k->event = ds3231_event;
    k->recv = ds3231_recv;
    k->send = ds3231_send;
}

static const TypeInfo ds3231_info = {
    .name = TYPE_DS3231,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(DS3231State),
    .class_init = ds3231_class_init,
};

static void ds3231_register_types(void)
{
    type_register_static(&ds3231_info);
}

type_init(ds3231_register_types)

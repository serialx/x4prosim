/*
 * TI BQ27220 fuel gauge on I2C (the Xteink X3's battery monitor).
 *
 * Standard commands as 16-bit little-endian words at their byte addresses:
 * Temperature (0x06), Voltage (0x08, "voltage-mv"), Current (0x0C,
 * "current-ma", signed: positive = charging, which the X3 firmware takes as
 * USB power and goes back to sleep on at boot), RemainingCapacity (0x10),
 * FullChargeCapacity (0x12), StateOfCharge (0x2C, "soc"), OperationStatus
 * (0x3A: SEC in bits 2:1, CFGUPDATE bit 10), DesignCapacity (0x3C).
 * Control (0x00) takes the UNSEAL keys (0414 3672), FULL_ACCESS (FFFF FFFF),
 * ENTER_CFG_UPDATE (0090), EXIT_CFG_UPDATE[_REINIT] (0092/0091) and SEAL
 * (0030). The data-memory window MACControl (0x3E) / MACData (0x40) /
 * MACDataSum (0x60) serves the Gas Gauging block's Design Capacity (0x929F)
 * and Learned FCC (0x929D) big-endian and accepts writes to them, with the
 * BQ27220 checksum (0xFF - sum of address and data bytes) and length 0x06.
 * Nothing else of the data memory or the gauging algorithm is modeled.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/i2c/i2c.h"
#include "hw/core/qdev-properties.h"

#define TYPE_BQ27220 "bq27220"
OBJECT_DECLARE_SIMPLE_TYPE(BQ27220State, BQ27220)

#define R_CONTROL       0x00
#define R_TEMPERATURE   0x06
#define R_VOLTAGE       0x08
#define R_CURRENT       0x0C
#define R_REMAINING     0x10
#define R_FCC           0x12
#define R_SOC           0x2C
#define R_OPSTATUS      0x3A
#define R_DESIGN_CAP    0x3C
#define R_MAC_CONTROL   0x3E
#define R_MAC_DATA      0x40
#define R_MAC_SUM       0x60
#define R_MAC_LEN       0x61

#define SEC_FULL        1
#define SEC_UNSEALED    2
#define SEC_SEALED      3
#define OPSTATUS_CFGUPDATE BIT(10)

#define DM_LEARNED_FCC  0x929D
#define DM_DESIGN_CAP   0x929F

struct BQ27220State {
    I2CSlave parent_obj;

    uint32_t voltage_mv;
    int32_t current_ma;
    uint32_t soc;
    uint32_t design_cap;
    uint32_t learned_fcc;
    uint8_t sec;
    bool cfgupdate;
    uint16_t last_key;
    uint16_t mac_addr;
    uint8_t mac_data[32];
    uint8_t regs[0x80];
    uint8_t ptr;
    int nwrite;
    uint16_t word;
};

static void bq27220_put16(BQ27220State *s, uint8_t reg, uint16_t v)
{
    s->regs[reg] = v;
    s->regs[reg + 1] = v >> 8;
}

static void bq27220_mac_load(BQ27220State *s)
{
    uint16_t v = s->mac_addr == DM_DESIGN_CAP ? s->design_cap
               : s->mac_addr == DM_LEARNED_FCC ? s->learned_fcc : 0;
    uint32_t sum = (s->mac_addr & 0xff) + (s->mac_addr >> 8);

    memset(s->mac_data, 0, sizeof(s->mac_data));
    s->mac_data[0] = v >> 8;
    s->mac_data[1] = v;
    for (int i = 0; i < 4; i++) {
        sum += s->mac_data[i];
    }
    s->regs[R_MAC_SUM] = 0xff - (sum & 0xff);
    s->regs[R_MAC_LEN] = 0x06;
}

static void bq27220_refresh(BQ27220State *s)
{
    bq27220_put16(s, R_TEMPERATURE, 2982);     /* 25 C in 0.1 K */
    bq27220_put16(s, R_VOLTAGE, s->voltage_mv);
    bq27220_put16(s, R_CURRENT, s->current_ma);
    bq27220_put16(s, R_FCC, s->learned_fcc);
    bq27220_put16(s, R_REMAINING, s->learned_fcc * MIN(s->soc, 100) / 100);
    bq27220_put16(s, R_SOC, s->soc);
    bq27220_put16(s, R_OPSTATUS, (s->sec << 1) | (s->cfgupdate ? OPSTATUS_CFGUPDATE : 0));
    bq27220_put16(s, R_DESIGN_CAP, s->design_cap);
    bq27220_put16(s, R_MAC_CONTROL, s->mac_addr);
    memcpy(&s->regs[R_MAC_DATA], s->mac_data, sizeof(s->mac_data));
}

static void bq27220_control(BQ27220State *s, uint16_t sub)
{
    switch (sub) {
    case 0x0030:    /* SEAL */
        s->sec = SEC_SEALED;
        break;
    case 0x0090:    /* ENTER_CFG_UPDATE */
        if (s->sec != SEC_SEALED) {
            s->cfgupdate = true;
        }
        break;
    case 0x0091:    /* EXIT_CFG_UPDATE_REINIT */
    case 0x0092:    /* EXIT_CFG_UPDATE */
        s->cfgupdate = false;
        break;
    case 0x3672:
        if (s->last_key == 0x0414 && s->sec == SEC_SEALED) {
            s->sec = SEC_UNSEALED;
        }
        break;
    case 0xFFFF:
        if (s->last_key == 0xFFFF && s->sec == SEC_UNSEALED) {
            s->sec = SEC_FULL;
        }
        break;
    }
    s->last_key = sub;
}

/* A write of a 16-bit word to register reg (the firmware writes whole words). */
static void bq27220_write_word(BQ27220State *s, uint8_t reg, uint16_t v)
{
    switch (reg) {
    case R_CONTROL:
        bq27220_control(s, v);
        break;
    case R_MAC_CONTROL:
        s->mac_addr = v;
        bq27220_mac_load(s);
        break;
    case R_MAC_DATA:
        s->mac_data[0] = v;
        s->mac_data[1] = v >> 8;
        break;
    case R_MAC_SUM:
        if (s->cfgupdate && s->sec == SEC_FULL) {
            uint16_t val = s->mac_data[0] << 8 | s->mac_data[1];
            if (s->mac_addr == DM_DESIGN_CAP) {
                s->design_cap = val;
            } else if (s->mac_addr == DM_LEARNED_FCC) {
                s->learned_fcc = val;
            }
        }
        break;
    }
}

static int bq27220_event(I2CSlave *i2c, enum i2c_event event)
{
    BQ27220State *s = BQ27220(i2c);

    switch (event) {
    case I2C_START_SEND:
        s->nwrite = 0;
        break;
    case I2C_START_RECV:
        bq27220_refresh(s);
        break;
    case I2C_FINISH:
        if (s->nwrite == 3) {
            bq27220_write_word(s, s->ptr, s->word);
        }
        break;
    default:
        break;
    }
    return 0;
}

static uint8_t bq27220_recv(I2CSlave *i2c)
{
    BQ27220State *s = BQ27220(i2c);
    uint8_t v = s->regs[s->ptr];

    s->ptr = (s->ptr + 1) % sizeof(s->regs);
    return v;
}

static int bq27220_send(I2CSlave *i2c, uint8_t data)
{
    BQ27220State *s = BQ27220(i2c);

    switch (s->nwrite++) {
    case 0: s->ptr = data % sizeof(s->regs); break;
    case 1: s->word = data; break;
    case 2: s->word |= data << 8; break;
    }
    return 0;
}

static void bq27220_reset_hold(Object *obj, ResetType type)
{
    BQ27220State *s = BQ27220(obj);

    s->sec = SEC_SEALED;
    s->cfgupdate = false;
    s->last_key = 0;
    s->learned_fcc = s->design_cap;
    s->mac_addr = 0;
    memset(s->regs, 0, sizeof(s->regs));
    bq27220_mac_load(s);
    bq27220_refresh(s);
}

static const Property bq27220_properties[] = {
    DEFINE_PROP_UINT32("voltage-mv", BQ27220State, voltage_mv, 3900),
    DEFINE_PROP_INT32("current-ma", BQ27220State, current_ma, -80),
    DEFINE_PROP_UINT32("soc", BQ27220State, soc, 80),
    DEFINE_PROP_UINT32("design-cap-mah", BQ27220State, design_cap, 650),

};

static void bq27220_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    k->event = bq27220_event;
    k->recv = bq27220_recv;
    k->send = bq27220_send;
    rc->phases.hold = bq27220_reset_hold;
    device_class_set_props(dc, bq27220_properties);
}

static const TypeInfo bq27220_info = {
    .name = TYPE_BQ27220,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(BQ27220State),
    .class_init = bq27220_class_init,
};

static void bq27220_register_types(void)
{
    type_register_static(&bq27220_info);
}

type_init(bq27220_register_types)

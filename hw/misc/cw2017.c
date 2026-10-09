/*
 * CellWise CW2017 I2C fuel gauge
 *
 * A 0x60-byte register file with an auto-incrementing pointer. Live
 * registers: VERSION 0x00 (0x0D while MODE is 0x00 "running", 0xA0 otherwise),
 * VCELL 0x02/0x03 (14-bit, 5/16 mV per LSB), SOC 0x04/0x05 and TEMP 0x06
 * (fixed 25 C). MODE 0x08, SOC_ALERT 0x0B and the BATINFO profile 0x10-0x5F
 * are plain storage, so the driver's profile upload and soft reset are
 * accepted. SOC reads 0 until a profile is flagged loaded (SOC_ALERT bit 7)
 * and the gauge is running, as on the chip after power-up. No fuel-gauge
 * algorithm: SoC and voltage come from the "soc" (percent) and "vcell-mv"
 * properties, settable with -global or at runtime with qom-set.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/i2c/i2c.h"
#include "qom/object.h"

#define TYPE_CW2017 "cw2017"
OBJECT_DECLARE_SIMPLE_TYPE(CW2017State, CW2017)

#define CW2017_NREGS        0x60
#define CW2017_VERSION      0x00
#define CW2017_VCELL_H      0x02
#define CW2017_VCELL_L      0x03
#define CW2017_SOC          0x04
#define CW2017_SOC_FRAC     0x05
#define CW2017_TEMP         0x06
#define CW2017_MODE         0x08
#define CW2017_SOC_ALERT    0x0b
#define CW2017_MODE_POR     0xf0    /* sleep + restart after power-up */
#define CW2017_PROFILE_SET  0x80

struct CW2017State {
    I2CSlave parent_obj;

    uint8_t regs[CW2017_NREGS];
    uint8_t ptr;
    bool ptr_next;      /* next byte written is the register pointer */
    uint8_t soc;
    uint16_t vcell_mv;
};

static uint8_t cw2017_reg(CW2017State *s, uint8_t reg)
{
    bool running = s->regs[CW2017_MODE] == 0;
    uint32_t vcell = MIN(s->vcell_mv * 16 / 5, 0x3fff);

    switch (reg) {
    case CW2017_VERSION:
        return running ? 0x0d : 0xa0;
    case CW2017_VCELL_H:
        return (vcell >> 8) & 0x3f;
    case CW2017_VCELL_L:
        return vcell & 0xff;
    case CW2017_SOC:
        return running && (s->regs[CW2017_SOC_ALERT] & CW2017_PROFILE_SET) ?
               MIN(s->soc, 100) : 0;
    case CW2017_SOC_FRAC:
        return 0;
    case CW2017_TEMP:
        return (25 + 40) * 2;
    default:
        return reg < CW2017_NREGS ? s->regs[reg] : 0;
    }
}

static int cw2017_event(I2CSlave *i2c, enum i2c_event event)
{
    CW2017State *s = CW2017(i2c);

    if (event == I2C_START_SEND) {
        s->ptr_next = true;
    }
    return 0;
}

static uint8_t cw2017_recv(I2CSlave *i2c)
{
    CW2017State *s = CW2017(i2c);

    return cw2017_reg(s, s->ptr++);
}

static int cw2017_send(I2CSlave *i2c, uint8_t data)
{
    CW2017State *s = CW2017(i2c);

    if (s->ptr_next) {
        s->ptr_next = false;
        s->ptr = data;
        return 0;
    }
    if (s->ptr < CW2017_NREGS) {
        s->regs[s->ptr] = data;
    }
    s->ptr++;
    return 0;
}

static void cw2017_init(Object *obj)
{
    CW2017State *s = CW2017(obj);

    /* Battery-backed: power-up state only, an SoC reset leaves it alone */
    s->regs[CW2017_MODE] = CW2017_MODE_POR;
    s->soc = 80;
    s->vcell_mv = 3950;
    object_property_add_uint8_ptr(obj, "soc", &s->soc, OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint16_ptr(obj, "vcell-mv", &s->vcell_mv, OBJ_PROP_FLAG_READWRITE);
}

static void cw2017_class_init(ObjectClass *klass, const void *data)
{
    I2CSlaveClass *sc = I2C_SLAVE_CLASS(klass);

    sc->event = cw2017_event;
    sc->recv = cw2017_recv;
    sc->send = cw2017_send;
}

static const TypeInfo cw2017_info = {
    .name = TYPE_CW2017,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(CW2017State),
    .instance_init = cw2017_init,
    .class_init = cw2017_class_init,
};

static void cw2017_register_types(void)
{
    type_register_static(&cw2017_info);
}

type_init(cw2017_register_types)

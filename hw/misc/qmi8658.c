/*
 * QST QMI8658 6-axis IMU on I2C (the Xteink X3's tilt sensor).
 *
 * WHO_AM_I (0x00) = 0x05, REVISION (0x01) = 0x7C, CTRL1-CTRL9 (0x02-0x0A)
 * stored, STATUS0 (0x2E) reports accel and gyro data available while CTRL7
 * enables them, TEMP (0x33-0x34) 25 C, accel AX..AZ (0x35-0x3A) reads the
 * device lying flat (Z = +1 g at the +-2 g scale) and gyro GX..GZ
 * (0x3B-0x40) reads at rest. The register pointer auto-increments. No FIFO,
 * motion engine or interrupts.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/i2c/i2c.h"

#define TYPE_QMI8658 "qmi8658"
OBJECT_DECLARE_SIMPLE_TYPE(QMI8658State, QMI8658)

#define QMI8658_NREGS   0x64
#define R_WHO_AM_I      0x00
#define R_REVISION      0x01
#define R_CTRL1         0x02
#define R_CTRL7         0x08
#define R_STATUS0       0x2E
#define R_TEMP_L        0x33
#define R_AX_L          0x35
#define R_GZ_H          0x40

struct QMI8658State {
    I2CSlave parent_obj;
    uint8_t regs[QMI8658_NREGS];
    uint8_t ptr;
    bool ptr_next;
};

static void qmi8658_latch(QMI8658State *s)
{
    uint8_t en = s->regs[R_CTRL7] & 3;

    s->regs[R_STATUS0] = en;
    s->regs[R_TEMP_L] = 0;
    s->regs[R_TEMP_L + 1] = 25;
    memset(&s->regs[R_AX_L], 0, R_GZ_H - R_AX_L + 1);
    s->regs[R_AX_L + 4] = 0x00;     /* AZ = 16384 = +1 g */
    s->regs[R_AX_L + 5] = 0x40;
}

static int qmi8658_event(I2CSlave *i2c, enum i2c_event event)
{
    QMI8658State *s = QMI8658(i2c);

    if (event == I2C_START_SEND) {
        s->ptr_next = true;
    } else if (event == I2C_START_RECV) {
        qmi8658_latch(s);
    }
    return 0;
}

static uint8_t qmi8658_recv(I2CSlave *i2c)
{
    QMI8658State *s = QMI8658(i2c);
    uint8_t val = s->regs[s->ptr];

    s->ptr = (s->ptr + 1) % QMI8658_NREGS;
    return val;
}

static int qmi8658_send(I2CSlave *i2c, uint8_t data)
{
    QMI8658State *s = QMI8658(i2c);

    if (s->ptr_next) {
        s->ptr_next = false;
        s->ptr = data % QMI8658_NREGS;
        return 0;
    }
    if (s->ptr >= R_CTRL1 && s->ptr < R_STATUS0) {
        s->regs[s->ptr] = data;
    }
    s->ptr = (s->ptr + 1) % QMI8658_NREGS;
    return 0;
}

static void qmi8658_reset_hold(Object *obj, ResetType type)
{
    QMI8658State *s = QMI8658(obj);

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[R_WHO_AM_I] = 0x05;
    s->regs[R_REVISION] = 0x7C;
    qmi8658_latch(s);
}

static void qmi8658_class_init(ObjectClass *klass, const void *data)
{
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    k->event = qmi8658_event;
    k->recv = qmi8658_recv;
    k->send = qmi8658_send;
    rc->phases.hold = qmi8658_reset_hold;
}

static const TypeInfo qmi8658_info = {
    .name = TYPE_QMI8658,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(QMI8658State),
    .class_init = qmi8658_class_init,
};

static void qmi8658_register_types(void)
{
    type_register_static(&qmi8658_info);
}

type_init(qmi8658_register_types)

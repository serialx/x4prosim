/*
 * Goodix GT911 capacitive touch controller (single contact + Home key)
 *
 * 16-bit register pointer, big-endian on the wire, auto-incrementing.
 * Models the command/config block 0x8040-0x8100 as storage, the read-only
 * product ID/resolution block at 0x8140, buffer status 0x814E (bit 7 frame
 * ready, bit 4 key down, low nibble contact count; a host write clears it)
 * and the first 8-byte point record 0x814F-0x8156. A new frame is latched when
 * input changes and the previous one has been cleared, and again after every
 * clear while a contact or the key is held. "int" idles low and pulses high
 * on each latch. GPIO inputs "power" (rail on when high) and "rst" (reset
 * while low) both default to running; while off or in reset the chip NACKs
 * its address, and leaving that state reloads the registers. The config is
 * not checked or applied, the sleep command (0x8040 = 5) is ignored because
 * the INT wake pulse is invisible here, and the address is fixed rather than
 * strapped by INT at reset.
 *
 * Input is in the controller's own coordinates, 480x800 portrait (X right, Y
 * down), which is how the x4pro panel console shows the screen. Sources: an
 * absolute pointer (left button = finger, right = Home), and write-only
 * properties for scripts:
 *   qom-set <path> touch X,Y | up     press/move, or lift
 *   qom-set <path> tap X,Y | home     100 ms tap on the screen or Home key
 *   qom-set <path> home on | off      hold or release Home
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "hw/i2c/i2c.h"
#include "hw/core/irq.h"
#include "hw/core/qdev.h"
#include "ui/input.h"

#define TYPE_GT911 "gt911"
OBJECT_DECLARE_SIMPLE_TYPE(GT911State, GT911)

#define GT911_BASE          0x8040
#define GT911_COMMAND       0x8040
#define GT911_CONFIG        0x8047
#define GT911_CONFIG_FRESH  0x8100
#define GT911_PRODUCT_ID    0x8140
#define GT911_STATUS        0x814e
#define GT911_POINT1        0x814f
#define GT911_END           0x8177
#define GT911_NREGS         (GT911_END - GT911_BASE)

#define GT911_STATUS_READY  0x80
#define GT911_STATUS_KEY    0x10

#define GT911_RES_X         480
#define GT911_RES_Y         800
#define GT911_TAP_MS        100

struct GT911State {
    I2CSlave parent_obj;

    QemuInputHandlerState *input;
    QEMUTimer *tap_timer;
    qemu_irq irq;
    uint8_t regs[GT911_NREGS];
    uint16_t ptr;
    int addr_bytes;     /* register address bytes still to come */
    /* live input */
    bool touch;
    bool key;
    uint16_t x;
    uint16_t y;
    bool changed;       /* live input differs from the latched frame */
    /* wire levels, not reset with the device */
    bool power;
    bool rst;
};

static uint8_t *gt911_reg(GT911State *s, uint16_t reg)
{
    return &s->regs[reg - GT911_BASE];
}

static void gt911_latch(GT911State *s)
{
    uint8_t *pt = gt911_reg(s, GT911_POINT1);

    *gt911_reg(s, GT911_STATUS) = GT911_STATUS_READY |
                                  (s->key ? GT911_STATUS_KEY : 0) | s->touch;
    memset(pt, 0, 8);
    if (s->touch) {
        stw_le_p(pt + 1, s->x);
        stw_le_p(pt + 3, s->y);
        stw_le_p(pt + 5, 0x20);     /* contact size */
    }
    s->changed = false;
    qemu_irq_pulse(s->irq);
}

static bool gt911_running(GT911State *s)
{
    return s->power && s->rst;
}

static void gt911_kick(GT911State *s)
{
    if (gt911_running(s) && s->changed &&
        !(*gt911_reg(s, GT911_STATUS) & GT911_STATUS_READY)) {
        gt911_latch(s);
    }
}

static void gt911_set_input(GT911State *s, bool touch, bool key)
{
    s->touch = touch;
    s->key = key;
    s->changed = true;
    gt911_kick(s);
}

static void gt911_write_reg(GT911State *s, uint16_t reg, uint8_t val)
{
    if (reg == GT911_STATUS) {
        *gt911_reg(s, GT911_STATUS) = 0;
        s->changed |= s->touch || s->key;
        gt911_kick(s);
    } else if (reg >= GT911_COMMAND && reg <= GT911_CONFIG_FRESH) {
        *gt911_reg(s, reg) = val;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "gt911: write to read-only 0x%04x\n", reg);
    }
}

static int gt911_event(I2CSlave *i2c, enum i2c_event event)
{
    GT911State *s = GT911(i2c);

    if ((event == I2C_START_SEND || event == I2C_START_RECV) && !gt911_running(s)) {
        return -1;
    }
    if (event == I2C_START_SEND) {
        s->addr_bytes = 2;
    }
    return 0;
}

static uint8_t gt911_recv(I2CSlave *i2c)
{
    GT911State *s = GT911(i2c);
    uint16_t reg = s->ptr++;

    return reg >= GT911_BASE && reg < GT911_END ? *gt911_reg(s, reg) : 0;
}

static int gt911_send(I2CSlave *i2c, uint8_t data)
{
    GT911State *s = GT911(i2c);

    if (s->addr_bytes) {
        s->ptr = (s->ptr << 8) | data;
        s->addr_bytes--;
    } else {
        gt911_write_reg(s, s->ptr++, data);
    }
    return 0;
}

static void gt911_input_event(DeviceState *dev, QemuConsole *src,
                              QemuInputEvent *evt)
{
    GT911State *s = GT911(dev);
    InputMoveEvent *move;
    InputBtnEvent *btn;

    switch (evt->type) {
    case INPUT_EVENT_KIND_ABS:
        move = &evt->abs;
        if (move->axis == INPUT_AXIS_X) {
            s->x = qemu_input_scale_axis(move->value, INPUT_EVENT_ABS_MIN,
                                         INPUT_EVENT_ABS_MAX, 0, GT911_RES_X - 1);
        } else if (move->axis == INPUT_AXIS_Y) {
            s->y = qemu_input_scale_axis(move->value, INPUT_EVENT_ABS_MIN,
                                         INPUT_EVENT_ABS_MAX, 0, GT911_RES_Y - 1);
        }
        s->changed |= s->touch;
        break;
    case INPUT_EVENT_KIND_BTN:
        btn = &evt->btn;
        if (btn->button == INPUT_BUTTON_LEFT && btn->down != s->touch) {
            s->touch = btn->down;
            s->changed = true;
        } else if (btn->button == INPUT_BUTTON_RIGHT && btn->down != s->key) {
            s->key = btn->down;
            s->changed = true;
        }
        break;
    default:
        break;
    }
}

static void gt911_input_sync(DeviceState *dev)
{
    gt911_kick(GT911(dev));
}

static const QemuInputHandler gt911_input_handler = {
    .name = "GT911 touchscreen",
    .mask = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_ABS,
    .event = gt911_input_event,
    .sync = gt911_input_sync,
};

static bool gt911_parse_point(GT911State *s, const char *str, Error **errp)
{
    unsigned x, y;
    char end;

    if (sscanf(str, "%u,%u%c", &x, &y, &end) != 2 ||
        x >= GT911_RES_X || y >= GT911_RES_Y) {
        error_setg(errp, "expected X,Y with X < %d and Y < %d", GT911_RES_X, GT911_RES_Y);
        return false;
    }
    s->x = x;
    s->y = y;
    return true;
}

static void gt911_set_touch(Object *obj, const char *str, Error **errp)
{
    GT911State *s = GT911(obj);

    if (!strcmp(str, "up")) {
        gt911_set_input(s, false, s->key);
    } else if (gt911_parse_point(s, str, errp)) {
        gt911_set_input(s, true, s->key);
    }
}

static void gt911_set_tap(Object *obj, const char *str, Error **errp)
{
    GT911State *s = GT911(obj);

    if (!strcmp(str, "home")) {
        gt911_set_input(s, s->touch, true);
    } else if (gt911_parse_point(s, str, errp)) {
        gt911_set_input(s, true, s->key);
    } else {
        return;
    }
    timer_mod(s->tap_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + GT911_TAP_MS);
}

static void gt911_tap_end(void *opaque)
{
    gt911_set_input(opaque, false, false);
}

static bool gt911_get_home(Object *obj, Error **errp)
{
    return GT911(obj)->key;
}

static void gt911_set_home(Object *obj, bool value, Error **errp)
{
    GT911State *s = GT911(obj);

    gt911_set_input(s, s->touch, value);
}

static void gt911_load_regs(GT911State *s)
{
    memset(s->regs, 0, sizeof(s->regs));
    memcpy(gt911_reg(s, GT911_PRODUCT_ID), "911", 4);
    stw_le_p(gt911_reg(s, GT911_PRODUCT_ID + 4), 0x1060);   /* firmware version */
    stw_le_p(gt911_reg(s, GT911_PRODUCT_ID + 6), GT911_RES_X);
    stw_le_p(gt911_reg(s, GT911_PRODUCT_ID + 8), GT911_RES_Y);
    *gt911_reg(s, GT911_CONFIG) = 0x41;                     /* config version */
    stw_le_p(gt911_reg(s, GT911_CONFIG + 1), GT911_RES_X);
    stw_le_p(gt911_reg(s, GT911_CONFIG + 3), GT911_RES_Y);
    *gt911_reg(s, GT911_CONFIG + 5) = 5;                    /* max contacts */
    s->ptr = 0;
    s->addr_bytes = 0;
}

static void gt911_set_wires(GT911State *s, bool power, bool rst)
{
    bool was_running = gt911_running(s);

    s->power = power;
    s->rst = rst;
    if (!was_running && gt911_running(s)) {
        /* Config self-loads and a finger still down is reported again */
        gt911_load_regs(s);
        qemu_irq_lower(s->irq);
        s->changed = s->touch || s->key;
        gt911_kick(s);
    }
}

static void gt911_power_in(void *opaque, int n, int level)
{
    GT911State *s = opaque;

    gt911_set_wires(s, level, s->rst);
}

static void gt911_rst_in(void *opaque, int n, int level)
{
    GT911State *s = opaque;

    gt911_set_wires(s, s->power, level);
}

static void gt911_reset_hold(Object *obj, ResetType type)
{
    GT911State *s = GT911(obj);

    gt911_load_regs(s);
    s->touch = false;
    s->key = false;
    s->changed = false;
    timer_del(s->tap_timer);
}

static void gt911_reset_exit(Object *obj, ResetType type)
{
    qemu_irq_lower(GT911(obj)->irq);
}

static void gt911_realize(DeviceState *dev, Error **errp)
{
    GT911State *s = GT911(dev);

    s->tap_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, gt911_tap_end, s);
    s->input = qemu_input_handler_register(dev, &gt911_input_handler);
    qemu_input_handler_activate(s->input);
}

static void gt911_unrealize(DeviceState *dev)
{
    GT911State *s = GT911(dev);

    qemu_input_handler_unregister(s->input);
    timer_free(s->tap_timer);
}

static void gt911_init(Object *obj)
{
    GT911State *s = GT911(obj);

    s->power = true;
    s->rst = true;
    qdev_init_gpio_out_named(DEVICE(obj), &s->irq, "int", 1);
    qdev_init_gpio_in_named(DEVICE(obj), gt911_power_in, "power", 1);
    qdev_init_gpio_in_named(DEVICE(obj), gt911_rst_in, "rst", 1);
}

static void gt911_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *sc = I2C_SLAVE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = gt911_realize;
    dc->unrealize = gt911_unrealize;
    sc->event = gt911_event;
    sc->recv = gt911_recv;
    sc->send = gt911_send;
    rc->phases.hold = gt911_reset_hold;
    rc->phases.exit = gt911_reset_exit;
    object_class_property_add_str(klass, "touch", NULL, gt911_set_touch);
    object_class_property_add_str(klass, "tap", NULL, gt911_set_tap);
    object_class_property_add_bool(klass, "home", gt911_get_home, gt911_set_home);
}

static const TypeInfo gt911_info = {
    .name = TYPE_GT911,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(GT911State),
    .instance_init = gt911_init,
    .class_init = gt911_class_init,
};

static void gt911_register_types(void)
{
    type_register_static(&gt911_info);
}

type_init(gt911_register_types)

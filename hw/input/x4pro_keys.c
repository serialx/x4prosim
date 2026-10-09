/*
 * Xteink X4 Pro physical keys: Up (GPIO0), Down (GPIO7), Power (GPIO3),
 * active low with pull-ups. Driven from the host keyboard (Up/Down arrows, P)
 * and from QOM bool properties "up", "down", "power" for scripted presses:
 *   qom-set /machine/x4pro-keys down true
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "ui/input.h"

#define TYPE_X4PRO_KEYS "x4pro-keys"
OBJECT_DECLARE_SIMPLE_TYPE(X4ProKeysState, X4PRO_KEYS)

enum { KEY_UP, KEY_DOWN, KEY_POWER, KEY_COUNT };

struct X4ProKeysState {
    SysBusDevice parent_obj;
    QemuInputHandlerState *input;
    qemu_irq out[KEY_COUNT];    /* pin level: 1 released, 0 pressed */
    bool pressed[KEY_COUNT];
};

static void keys_set(X4ProKeysState *s, int k, bool down)
{
    s->pressed[k] = down;
    qemu_set_irq(s->out[k], !down);
}

static void keys_event(DeviceState *dev, QemuConsole *src, QemuInputEvent *evt)
{
    X4ProKeysState *s = X4PRO_KEYS(dev);
    QemuInputKeyEvent *key = &evt->key;
    int qcode = qemu_input_linux_to_qcode(key->key);
    int k = qcode == Q_KEY_CODE_UP ? KEY_UP : qcode == Q_KEY_CODE_DOWN ? KEY_DOWN
          : qcode == Q_KEY_CODE_P ? KEY_POWER : -1;

    if (k >= 0) {
        keys_set(s, k, key->down);
    }
}

static const QemuInputHandler keys_handler = {
    .name = "X4 Pro keys",
    .mask = INPUT_EVENT_MASK_KEY,
    .event = keys_event,
};

#define KEY_PROP(name, idx)                                                    \
    static bool get_##name(Object *o, Error **e) { return X4PRO_KEYS(o)->pressed[idx]; } \
    static void set_##name(Object *o, bool v, Error **e) { keys_set(X4PRO_KEYS(o), idx, v); }
KEY_PROP(up, KEY_UP)
KEY_PROP(down, KEY_DOWN)
KEY_PROP(power, KEY_POWER)

static void keys_realize(DeviceState *dev, Error **errp)
{
    X4ProKeysState *s = X4PRO_KEYS(dev);
    s->input = qemu_input_handler_register(dev, &keys_handler);
    for (int k = 0; k < KEY_COUNT; k++) {
        keys_set(s, k, false);
    }
}

static void keys_unrealize(DeviceState *dev)
{
    X4ProKeysState *s = X4PRO_KEYS(dev);

    qemu_input_handler_unregister(s->input);
}

static void keys_init(Object *obj)
{
    X4ProKeysState *s = X4PRO_KEYS(obj);
    qdev_init_gpio_out(DEVICE(obj), s->out, KEY_COUNT);
    object_property_add_bool(obj, "up", get_up, set_up);
    object_property_add_bool(obj, "down", get_down, set_down);
    object_property_add_bool(obj, "power", get_power, set_power);
}

static void keys_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = keys_realize;
    dc->unrealize = keys_unrealize;
}

static const TypeInfo keys_info = {
    .name = TYPE_X4PRO_KEYS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(X4ProKeysState),
    .instance_init = keys_init,
    .class_init = keys_class_init,
};

static void keys_register_types(void)
{
    type_register_static(&keys_info);
}

type_init(keys_register_types)

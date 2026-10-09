/*
 * ESP32-C3 TWAI (Two-Wire Automotive Interface) emulation
 *
 * Copyright (c) 2025 Espressif Systems (Shanghai) Co. Ltd.
 *
 * The ESP32-C3 TWAI peripheral is identical to ESP32 TWAI controller.
 * It supports standard frame format (11-bit ID) and extended frame format
 * (29-bit ID) with programmable bit rate up to 1 Mbps.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/net/can/esp32c3_twai.h"

static uint64_t esp32c3_twai_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32C3TWAIClass *class = ESP32C3_TWAI_GET_CLASS(opaque);

    return class->parent_twai_read(opaque, addr, size);
}

static void esp32c3_twai_write(void *opaque, hwaddr addr, uint64_t value,
                               unsigned int size)
{
    Esp32C3TWAIClass *class = ESP32C3_TWAI_GET_CLASS(opaque);
    class->parent_twai_write(opaque, addr, value, size);
}

static void esp32c3_twai_realize(DeviceState *dev, Error **errp)
{
    Esp32C3TWAIClass *esp32c3_class = ESP32C3_TWAI_GET_CLASS(dev);
    Esp32TWAIState *s = ESP32_TWAI(dev);

    esp32c3_class->parent_realize(dev, errp);

    /* ESP32-C3 hardware defaults to PeliCAN mode and doesn't support BasicCAN */
    s->sja_state.clock |= 0x80;
}

static void esp32c3_twai_reset(Object *obj, ResetType type)
{
    Esp32C3TWAIState *s = ESP32C3_TWAI(obj);
    Esp32C3TWAIClass *esp32c3_class = ESP32C3_TWAI_GET_CLASS(obj);

    esp32c3_class->parent_reset(obj, type);

    /* ESP32-C3 hardware is always in PeliCAN mode */
    s->parent.sja_state.clock |= 0x80;
}

static void esp32c3_twai_init(Object *obj)
{
    /* Parent class init is called automatically by QOM */
}

static void esp32c3_twai_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    Esp32C3TWAIClass *esp32c3_class = ESP32C3_TWAI_CLASS(klass);
    Esp32TWAIClass *esp32_class = ESP32_TWAI_CLASS(klass);

    device_class_set_parent_realize(dc, esp32c3_twai_realize, &esp32c3_class->parent_realize);

    esp32c3_class->parent_reset = rc->phases.hold;
    rc->phases.hold = esp32c3_twai_reset;

    esp32c3_class->parent_twai_write = esp32_class->twai_write;
    esp32c3_class->parent_twai_read = esp32_class->twai_read;

    esp32_class->twai_write = esp32c3_twai_write;
    esp32_class->twai_read = esp32c3_twai_read;
}

static const TypeInfo esp32c3_twai_type_info = {
    .name = TYPE_ESP32C3_TWAI,
    .parent = TYPE_ESP32_TWAI,
    .instance_size = sizeof(Esp32C3TWAIState),
    .instance_init = esp32c3_twai_init,
    .class_size = sizeof(Esp32C3TWAIClass),
    .class_init = esp32c3_twai_class_init,
};

static void esp32c3_twai_register_types(void)
{
    type_register_static(&esp32c3_twai_type_info);
}

type_init(esp32c3_twai_register_types)

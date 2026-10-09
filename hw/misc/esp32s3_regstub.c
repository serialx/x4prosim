/*
 * ESP32-S3 register-file stub for blocks QEMU doesn't model (radio, analog).
 *
 * Writes are kept and read back, so read-modify-write code works, and listed
 * status bits always read as set ("or-offsets"/"or-masks" pairs), so code
 * that polls for a done/ready bit moves on. Nothing behind it works: the
 * Wi-Fi/BT radio is not emulated; this only keeps its init from hanging.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"

#define TYPE_ESP32S3_REGSTUB "misc.esp32s3.regstub"
OBJECT_DECLARE_SIMPLE_TYPE(Esp32s3RegstubState, ESP32S3_REGSTUB)

struct Esp32s3RegstubState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t size;
    uint32_t *regs;
    uint32_t n_off, n_mask;
    uint32_t *or_off, *or_mask;
};

static uint64_t regstub_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32s3RegstubState *s = opaque;
    uint32_t r = s->regs[addr / 4];
    for (uint32_t i = 0; i < s->n_off && i < s->n_mask; i++) {
        if (s->or_off[i] == addr) {
            r |= s->or_mask[i];
        }
    }
    return r;
}

static void regstub_write(void *opaque, hwaddr addr, uint64_t value, unsigned int size)
{
    Esp32s3RegstubState *s = opaque;
    s->regs[addr / 4] = value;
}

static const MemoryRegionOps regstub_ops = {
    .read = regstub_read,
    .write = regstub_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void regstub_realize(DeviceState *dev, Error **errp)
{
    Esp32s3RegstubState *s = ESP32S3_REGSTUB(dev);
    s->regs = g_new0(uint32_t, s->size / 4);
    memory_region_init_io(&s->iomem, OBJECT(dev), &regstub_ops, s, TYPE_ESP32S3_REGSTUB, s->size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static const Property regstub_properties[] = {
    DEFINE_PROP_UINT32("size", Esp32s3RegstubState, size, 0x1000),
    DEFINE_PROP_ARRAY("or-offsets", Esp32s3RegstubState, n_off, or_off, qdev_prop_uint32, uint32_t),
    DEFINE_PROP_ARRAY("or-masks", Esp32s3RegstubState, n_mask, or_mask, qdev_prop_uint32, uint32_t),

};

static void regstub_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = regstub_realize;
    device_class_set_props(dc, regstub_properties);
}

static const TypeInfo regstub_info = {
    .name = TYPE_ESP32S3_REGSTUB,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32s3RegstubState),
    .class_init = regstub_class_init,
};

static void regstub_register_types(void)
{
    type_register_static(&regstub_info);
}

type_init(regstub_register_types)

/*
 * ESP32-S3 RTC SENS (SAR ADC oneshot) registers.
 *
 * Plain register file, except the two oneshot result registers always read as
 * "conversion done" with a mid-scale sample, so ADC self-calibration and
 * analogRead() finish instead of polling forever, and the on-chip temperature
 * sensor (TSENS_CTRL) is always ready with a reading of about 25 C.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"

#define TYPE_ESP32S3_SENS "misc.esp32s3.sens"
OBJECT_DECLARE_SIMPLE_TYPE(Esp32s3SensState, ESP32S3_SENS)

#define SENS_REGS_SIZE         0x400
#define R_SAR_MEAS1_CTRL2      0x0C
#define R_SAR_MEAS2_CTRL2      0x30
#define MEAS_DONE              BIT(16)
#define MEAS_DATA_MASK         0xffff
#define R_SAR_TSENS_CTRL       0x50
#define TSENS_READY            BIT(8)
#define TSENS_RAW_25C          104     /* IDF: 0.4386 * raw - 20.52 = 25 C */

struct Esp32s3SensState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t regs[SENS_REGS_SIZE / 4];
    uint32_t adc_raw;
};

static uint64_t sens_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32s3SensState *s = opaque;
    uint32_t r = s->regs[addr / 4];
    if (addr == R_SAR_MEAS1_CTRL2 || addr == R_SAR_MEAS2_CTRL2) {
        r = (r & ~MEAS_DATA_MASK) | MEAS_DONE | (s->adc_raw & MEAS_DATA_MASK);
    }
    if (addr == R_SAR_TSENS_CTRL) {
        r = (r & ~0xff) | TSENS_READY | TSENS_RAW_25C;
    }
    return r;
}

static void sens_write(void *opaque, hwaddr addr, uint64_t value, unsigned int size)
{
    Esp32s3SensState *s = opaque;
    s->regs[addr / 4] = value;
}

static const MemoryRegionOps sens_ops = {
    .read = sens_read,
    .write = sens_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void sens_init(Object *obj)
{
    Esp32s3SensState *s = ESP32S3_SENS(obj);
    memory_region_init_io(&s->iomem, obj, &sens_ops, s, TYPE_ESP32S3_SENS, SENS_REGS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const Property sens_properties[] = {
    DEFINE_PROP_UINT32("adc_raw", Esp32s3SensState, adc_raw, 2048),

};

static void sens_class_init(ObjectClass *klass, const void *data)
{
    device_class_set_props(DEVICE_CLASS(klass), sens_properties);
}

static const TypeInfo sens_info = {
    .name = TYPE_ESP32S3_SENS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32s3SensState),
    .instance_init = sens_init,
    .class_init = sens_class_init,
};

static void sens_register_types(void)
{
    type_register_static(&sens_info);
}

type_init(sens_register_types)

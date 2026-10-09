/*
 * ESP32-C3 SAR ADC (APB_SARADC), one-shot sampling only.
 *
 * ONETIME_SAMPLE (0x20) START with ADC1/ADC2_ONETIME_SAMPLE set samples the
 * selected channel: 1_DATA_STATUS / 2_DATA_STATUS (0x2C/0x30) get the raw
 * 12-bit value and INT_RAW (0x44) ADC1_DONE / ADC2_DONE (bits 31/30) rise.
 * The channel values come from the board over the "adc1" (5 lines, GPIO0-4)
 * and "adc2" (1 line, GPIO5) GPIO inputs, whose level is the raw count;
 * an undriven channel reads 0. The DMA/pattern-table mode, filters,
 * thresholds and the temperature sensor are stored but not modeled.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"

#define TYPE_ESP32C3_SARADC "misc.esp32c3.saradc"
OBJECT_DECLARE_SIMPLE_TYPE(Esp32c3SaradcState, ESP32C3_SARADC)

#define R_ONETIME_SAMPLE    0x20
#define R_1_DATA_STATUS     0x2C
#define R_2_DATA_STATUS     0x30
#define R_INT_ENA           0x40
#define R_INT_RAW           0x44
#define R_INT_ST            0x48
#define R_INT_CLR           0x4C
#define R_DATE              0x3FC
#define REGS_SIZE           0x400

#define ONETIME_ADC1        BIT(31)
#define ONETIME_ADC2        BIT(30)
#define ONETIME_START       BIT(29)
#define ONETIME_CHANNEL(v)  (((v) >> 25) & 0xf)
#define INT_ADC1_DONE       BIT(31)
#define INT_ADC2_DONE       BIT(30)

#define ADC1_CHANNELS 5
#define ADC2_CHANNELS 1

struct Esp32c3SaradcState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;
    uint32_t regs[0x64 / 4];
    uint32_t int_raw;
    uint32_t int_ena;
    uint32_t data[2];
    uint32_t raw1[ADC1_CHANNELS];
    uint32_t raw2[ADC2_CHANNELS];
};

static void saradc_sample(Esp32c3SaradcState *s, uint32_t v)
{
    int ch = ONETIME_CHANNEL(v);
    int unit = ch >> 3;
    ch &= 7;

    if (unit == 0 && (v & ONETIME_ADC1)) {
        s->data[0] = ch < ADC1_CHANNELS ? s->raw1[ch] & 0xfff : 0;
        s->int_raw |= INT_ADC1_DONE;
    } else if (unit == 1 && (v & ONETIME_ADC2)) {
        s->data[1] = ch < ADC2_CHANNELS ? s->raw2[ch] & 0xfff : 0;
        s->int_raw |= INT_ADC2_DONE;
    }
}

static uint64_t saradc_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32c3SaradcState *s = opaque;

    switch (addr) {
    case R_1_DATA_STATUS: return s->data[0];
    case R_2_DATA_STATUS: return s->data[1];
    case R_INT_ENA:       return s->int_ena;
    case R_INT_RAW:       return s->int_raw;
    case R_INT_ST:        return s->int_raw & s->int_ena;
    case R_DATE:          return 0x2007211;
    }
    return addr < sizeof(s->regs) ? s->regs[addr / 4] : 0;
}

static void saradc_write(void *opaque, hwaddr addr, uint64_t value, unsigned int size)
{
    Esp32c3SaradcState *s = opaque;

    switch (addr) {
    case R_ONETIME_SAMPLE:
        if ((value & ONETIME_START) && !(s->regs[addr / 4] & ONETIME_START)) {
            saradc_sample(s, value);
        }
        s->regs[addr / 4] = value;
        break;
    case R_INT_ENA: s->int_ena = value; break;
    case R_INT_CLR: s->int_raw &= ~value; break;
    default:
        if (addr < sizeof(s->regs)) {
            s->regs[addr / 4] = value;
        }
        break;
    }
    qemu_set_irq(s->irq, (s->int_raw & s->int_ena) != 0);
}

static const MemoryRegionOps saradc_ops = {
    .read = saradc_read,
    .write = saradc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void saradc_set_adc1(void *opaque, int n, int level)
{
    ESP32C3_SARADC(opaque)->raw1[n] = level;
}

static void saradc_set_adc2(void *opaque, int n, int level)
{
    ESP32C3_SARADC(opaque)->raw2[n] = level;
}

static void saradc_init(Object *obj)
{
    Esp32c3SaradcState *s = ESP32C3_SARADC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &saradc_ops, s, TYPE_ESP32C3_SARADC, REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_in_named(DEVICE(obj), saradc_set_adc1, "adc1", ADC1_CHANNELS);
    qdev_init_gpio_in_named(DEVICE(obj), saradc_set_adc2, "adc2", ADC2_CHANNELS);
}

static const TypeInfo saradc_info = {
    .name = TYPE_ESP32C3_SARADC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32c3SaradcState),
    .instance_init = saradc_init,
};

static void saradc_register_types(void)
{
    type_register_static(&saradc_info);
}

type_init(saradc_register_types)

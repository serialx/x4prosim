/*
 * ESP32-S3 Bluetooth controller register interface.
 *
 * Models exchange-memory configuration and the sampled Bluetooth clock.
 * RF packet transmission and reception are not implemented.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/core/sysbus.h"

#define TYPE_ESP32S3_BLE "misc.esp32s3.ble"
OBJECT_DECLARE_SIMPLE_TYPE(Esp32s3BleState, ESP32S3_BLE)

#define REG_SIZE 0x1000
#define R_CONTROL 0x00
#define R_VERSION 0x04
#define R_BASETIMECNT 0x1c
#define R_FINETIMECNT 0x20
#define SAMPLE_CLOCK BIT(31)
#define SOFT_RESET BIT(31)

struct Esp32s3BleState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t regs[REG_SIZE / 4];
};

static uint64_t ble_read(void *opaque, hwaddr addr, unsigned size)
{
    Esp32s3BleState *s = opaque;

    if (addr == R_VERSION) {
        return 0x09001b00;
    }
    return s->regs[addr / 4];
}

static void ble_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    Esp32s3BleState *s = opaque;

    if (addr == R_VERSION) {
        return;
    }
    if (addr == R_CONTROL) {
        /* The controller's software-reset request completes synchronously. */
        s->regs[addr / 4] = value & ~SOFT_RESET;
    } else if (addr == R_BASETIMECNT && (value & SAMPLE_CLOCK)) {
        /* Half-slot count and a descending half-microsecond fine counter. */
        uint64_t ticks = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / 500;
        s->regs[R_BASETIMECNT / 4] = (ticks / 625) & 0x0fffffff;
        s->regs[R_FINETIMECNT / 4] = 624 - ticks % 625;
    } else {
        s->regs[addr / 4] = value;
    }
}

static const MemoryRegionOps ble_ops = {
    .read = ble_read,
    .write = ble_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void ble_reset(DeviceState *dev)
{
    memset(ESP32S3_BLE(dev)->regs, 0, sizeof(ESP32S3_BLE(dev)->regs));
}

static void ble_init(Object *obj)
{
    Esp32s3BleState *s = ESP32S3_BLE(obj);

    memory_region_init_io(&s->iomem, obj, &ble_ops, s, TYPE_ESP32S3_BLE, REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void ble_class_init(ObjectClass *klass, const void *data)
{
    device_class_set_legacy_reset(DEVICE_CLASS(klass), ble_reset);
}

static const TypeInfo ble_info = {
    .name = TYPE_ESP32S3_BLE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32s3BleState),
    .instance_init = ble_init,
    .class_init = ble_class_init,
};

static void ble_register_types(void)
{
    type_register_static(&ble_info);
}

type_init(ble_register_types)

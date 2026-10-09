/*
 * ESP32 TWAI (Two-Wire Automotive Interface) emulation
 *
 * Copyright (c) 2025 Espressif Systems (Shanghai) Co. Ltd.
 *
 * The ESP32 TWAI peripheral is a CAN 2.0B controller based on SJA1000.
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
#include "hw/net/can/esp32_twai.h"
#include "hw/net/can/can_sja1000.h"
#include "qom/object.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "net/can_emu.h"

/* Migration state description */
static const VMStateDescription vmstate_esp32_twai = {
    .name = TYPE_ESP32_TWAI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(sja_state, Esp32TWAIState, 0, vmstate_can_sja, CanSJA1000State),
        VMSTATE_END_OF_LIST()
    }
};

/* Reset handler */
static void esp32_twai_reset(Object *obj, ResetType type)
{
    Esp32TWAIState *s = ESP32_TWAI(obj);

    /* Reset underlying SJA1000 hardware to its default state */
    can_sja_hardware_reset(&s->sja_state);
}

/* Interrupt handler for SJA1000 events */
static void esp32_twai_irq_handler(void *opaque, int irq_num, int level)
{
    Esp32TWAIState *d = opaque;

    qemu_set_irq(d->irq, level);
}

/* Memory-mapped I/O read handler for the TWAI peripheral.
 * Maps ESP32 TWAI register accesses to the underlying SJA1000 controller.
 */
static uint64_t esp32_twai_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32TWAIState *s = ESP32_TWAI(opaque);
    /*
    * ESP32 TWAI registers are 32-bit aligned, but SJA1000 expects byte offsets.
    * Shift addr right by 2 to convert from word address to SJA1000 register index.
    */
    const uint64_t sja_addr = addr >> 2;
    uint8_t value;
    if ((s->sja_state.clock & 0x80) && sja_addr == SJA_RMC) {
        /* PeliCAN Mode */
        value = s->sja_state.rxmsg_cnt;
    } else {
        value = can_sja_mem_read(&s->sja_state, sja_addr, 1) & 0xFF;
    }

    return value;
}

/* Memory-mapped I/O write handler for the TWAI peripheral.
 * Maps ESP32 TWAI register accesses to the underlying SJA1000 controller.
 */
static void esp32_twai_write(void *opaque, hwaddr addr, uint64_t value,
                            unsigned int size)
{
    Esp32TWAIState *s = ESP32_TWAI(opaque);
    /*
    * ESP32 TWAI registers are 32-bit aligned, but SJA1000 expects byte offsets.
    * Shift addr right by 2 to convert from word address to SJA1000 register index.
    */
    const uint64_t sja_addr = addr >> 2;

    if (sja_addr == SJA_CDR) {
        value |= 0x80;
    }

    can_sja_mem_write(&s->sja_state, sja_addr, value, size);
}

static void esp32_twai_init(Object * obj)
{
    Esp32TWAIState *s = ESP32_TWAI(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    Esp32TWAIClass *twai_class = ESP32_TWAI_GET_CLASS(obj);

    /* Set up MMIO operations */
    s->twai_ops = (MemoryRegionOps) {
        .read = twai_class->twai_read,
        .write = twai_class->twai_write,
        .endianness = DEVICE_LITTLE_ENDIAN,
    };

    /* Initialize MMIO region */
    memory_region_init_io(&s->iomem, obj, &s->twai_ops, s,
                         TYPE_ESP32_TWAI, ESP32_TWAI_MEM_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    /* Add CAN bus link property */
    object_property_add_link(obj, "canbus", TYPE_CAN_BUS,
                           (Object **)&s->canbus,
                           qdev_prop_allow_set_link_before_realize,
                           0);
}

/* Device realization */
static void esp32_twai_realize(DeviceState *dev, Error **errp)
{
    Esp32TWAIState *s = ESP32_TWAI(dev);

    /* Set up interrupt handling */
    s->irq_handler = qemu_allocate_irq(esp32_twai_irq_handler, s, 0);

    /* Initialize SJA1000 controller */
    can_sja_init(&s->sja_state, s->irq_handler);

    /* Connect to CAN bus */
    if (s->canbus) {
        if (can_sja_connect_to_bus(&s->sja_state, s->canbus) < 0) {
            error_setg(errp, "Failed to connect TWAI to CAN bus");
            return;
        }
    }
}

/* Class initialization */
static void esp32_twai_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    Esp32TWAIClass *twai_class = ESP32_TWAI_CLASS(klass);

    /* Set up virtual methods */
    twai_class->twai_read = esp32_twai_read;
    twai_class->twai_write = esp32_twai_write;

    /* Set up device class methods */
    rc->phases.hold = esp32_twai_reset;
    dc->realize = esp32_twai_realize;
    dc->vmsd = &vmstate_esp32_twai;
}

static const TypeInfo esp32_twai_type_info = {
    .name = TYPE_ESP32_TWAI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32TWAIState),
    .instance_init = esp32_twai_init,
    .class_size = sizeof(Esp32TWAIClass),
    .class_init = esp32_twai_class_init,
};

static void esp32_twai_register_types(void)
{
    type_register_static(&esp32_twai_type_info);
}

type_init(esp32_twai_register_types)

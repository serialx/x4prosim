/*
 * ESP32-S3 USB Serial/JTAG controller: CDC serial side only.
 *
 * Bytes the firmware writes to EP1 go to the "chardev" backend; bytes from the
 * backend are readable from EP1. The SOF status bit always reads as set, a
 * 1 kHz SOF interrupt ticks and the frame counter follows virtual time, so
 * Arduino's HWCDC and IDF's plug watchdog always see a connected host.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/fifo8.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "chardev/char-fe.h"

#define TYPE_ESP32S3_USB_JTAG "misc.esp32s3.usb_serial_jtag"
OBJECT_DECLARE_SIMPLE_TYPE(Esp32s3UsbJtagState, ESP32S3_USB_JTAG)

#define R_EP1       0x00
#define R_EP1_CONF  0x04
#define R_INT_RAW   0x08
#define R_INT_ST    0x0C
#define R_INT_ENA   0x10
#define R_INT_CLR   0x14
#define R_FRAM_NUM  0x24
#define R_DATE      0x80
#define REGS_SIZE   0x84

#define CONF_WR_DONE        BIT(0)
#define CONF_IN_DATA_FREE   BIT(1)
#define CONF_OUT_DATA_AVAIL BIT(2)

#define INT_SOF             BIT(1)
#define INT_OUT_RECV_PKT    BIT(2)
#define INT_IN_EMPTY        BIT(3)

#define RX_FIFO_SIZE 64

struct Esp32s3UsbJtagState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;
    CharFrontend chr;
    QEMUTimer sof_timer;
    Fifo8 rx;
    uint32_t int_raw;
    uint32_t int_ena;
    uint32_t frame;
};

/*
 * A host that is plugged in sends a SOF every millisecond, so the SOF status
 * bit reads as set whenever the guest looks: IDF's plug watchdog clears it in
 * every FreeRTOS tick hook and declares the host gone after five hooks that
 * found it clear. QEMU can run several ticks inside one virtual millisecond
 * (bunched timer deadlines), so a SOF that only came from the 1 ms timer went
 * missing often enough that Arduino's HWCDC marked the link down and cut a
 * 52 KB screenshot write to its 256-byte ring. Unplugging isn't modeled.
 */
static uint32_t usb_jtag_sof_pending(Esp32s3UsbJtagState *s)
{
    return INT_SOF;
}

static void usb_jtag_update_irq(Esp32s3UsbJtagState *s)
{
    if (!fifo8_is_empty(&s->rx)) {
        s->int_raw |= INT_OUT_RECV_PKT;
    }
    qemu_set_irq(s->irq, ((s->int_raw | usb_jtag_sof_pending(s)) & s->int_ena) != 0);
}

static uint64_t usb_jtag_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32s3UsbJtagState *s = opaque;
    uint32_t r = 0;

    switch (addr) {
    case R_EP1:
        if (!fifo8_is_empty(&s->rx)) {
            r = fifo8_pop(&s->rx);
            qemu_chr_fe_accept_input(&s->chr);
        }
        break;
    case R_EP1_CONF:
        /* TX is drained instantly, so the IN FIFO always has room. */
        r = CONF_IN_DATA_FREE | (fifo8_is_empty(&s->rx) ? 0 : CONF_OUT_DATA_AVAIL);
        break;
    case R_INT_RAW:
        r = s->int_raw | usb_jtag_sof_pending(s);
        break;
    case R_INT_ST:
        r = (s->int_raw | usb_jtag_sof_pending(s)) & s->int_ena;
        break;
    case R_INT_ENA:
        r = s->int_ena;
        break;
    case R_FRAM_NUM:
        /*
         * The host's SOF count follows virtual time, not the 1 ms timer: the
         * guest's plug watchdog samples it every tick and would see a stalled
         * counter whenever the main loop lagged, then drop its TX ring.
         */
        r = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) & 0x7ff;
        break;
    case R_DATE:
        r = 0x2101200;
        break;
    }
    return r;
}

static void usb_jtag_write(void *opaque, hwaddr addr, uint64_t value, unsigned int size)
{
    Esp32s3UsbJtagState *s = opaque;
    uint8_t ch;

    switch (addr) {
    case R_EP1:
        ch = value & 0xff;
        qemu_chr_fe_write_all(&s->chr, &ch, 1);
        break;
    case R_EP1_CONF:
        if (value & CONF_WR_DONE) {
            s->int_raw |= INT_IN_EMPTY;
        }
        break;
    case R_INT_ENA:
        s->int_ena = value;
        break;
    case R_INT_CLR:
        s->int_raw &= ~value;
        /* The IN FIFO is always empty here, so the status comes straight back. */
        s->int_raw |= INT_IN_EMPTY;
        break;
    }
    usb_jtag_update_irq(s);
}

static const MemoryRegionOps usb_jtag_ops = {
    .read = usb_jtag_read,
    .write = usb_jtag_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void usb_jtag_sof(void *opaque)
{
    Esp32s3UsbJtagState *s = opaque;
    s->frame++;
    s->int_raw |= INT_SOF;
    usb_jtag_update_irq(s);
    timer_mod(&s->sof_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
}

static int usb_jtag_can_receive(void *opaque)
{
    Esp32s3UsbJtagState *s = opaque;
    return fifo8_num_free(&s->rx);
}

static void usb_jtag_receive(void *opaque, const uint8_t *buf, int size)
{
    Esp32s3UsbJtagState *s = opaque;
    fifo8_push_all(&s->rx, buf, size);
    usb_jtag_update_irq(s);
}

static void usb_jtag_reset_hold(Object *obj, ResetType type)
{
    Esp32s3UsbJtagState *s = ESP32S3_USB_JTAG(obj);
    s->int_raw = INT_IN_EMPTY;
    s->int_ena = 0;
    s->frame = 0;
    fifo8_reset(&s->rx);
    timer_mod(&s->sof_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
}

static void usb_jtag_realize(DeviceState *dev, Error **errp)
{
    Esp32s3UsbJtagState *s = ESP32S3_USB_JTAG(dev);
    qemu_chr_fe_set_handlers(&s->chr, usb_jtag_can_receive, usb_jtag_receive,
                             NULL, NULL, s, NULL, true);
}

static void usb_jtag_init(Object *obj)
{
    Esp32s3UsbJtagState *s = ESP32S3_USB_JTAG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &usb_jtag_ops, s, TYPE_ESP32S3_USB_JTAG, REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    fifo8_create(&s->rx, RX_FIFO_SIZE);
    timer_init_ms(&s->sof_timer, QEMU_CLOCK_VIRTUAL, usb_jtag_sof, s);
}

static const Property usb_jtag_properties[] = {
    DEFINE_PROP_CHR("chardev", Esp32s3UsbJtagState, chr),

};

static void usb_jtag_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = usb_jtag_reset_hold;
    dc->realize = usb_jtag_realize;
    device_class_set_props(dc, usb_jtag_properties);
}

static const TypeInfo usb_jtag_info = {
    .name = TYPE_ESP32S3_USB_JTAG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32s3UsbJtagState),
    .instance_init = usb_jtag_init,
    .class_init = usb_jtag_class_init,
};

static void usb_jtag_register_types(void)
{
    type_register_static(&usb_jtag_info);
}

type_init(usb_jtag_register_types)

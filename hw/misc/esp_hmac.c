/*
 * ESP HMAC emulation
 *
 * Copyright (c) 2025 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "hw/nvram/esp_efuse.h"
#include "hw/misc/esp_hmac.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"

#define HMAC_WARNING 0
#define HMAC_DEBUG   0

static void esp_hmac_start(ESPHmacState *s)
{
    uint8_t efuse_key[32];
    ESPEfuseClass *efuse_class = ESP_EFUSE_GET_CLASS(s->efuse);
    efuse_class->get_key(s->efuse, s->efuse_block_num, efuse_key);
    hmac_sha256_init(&s->ctx, efuse_key, sizeof(efuse_key));
    s->message_write_complete = 0;
}


static void esp_hmac_update(ESPHmacState *s, uint32_t *message)
{
    hmac_sha256_update(&s->ctx, (uint8_t*)(message));
}


static void esp_hmac_finish(ESPHmacState *s, uint32_t *result)
{
    hmac_sha256_final(&s->ctx, (uint8_t*) result, sizeof(result));
}


static uint64_t esp_hmac_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESPHmacClass *class = ESP_HMAC_GET_CLASS(opaque);
    ESPHmacState *s = ESP_HMAC(opaque);
    ESPEfuseClass *efuse_class = ESP_EFUSE_GET_CLASS(s->efuse);

    uint64_t r = 0;
    switch (addr) {
        case A_HMAC_DATE_REG:
            r = class->date;
            break;

        case A_HMAC_QUERY_ERROR_REG:
            r = efuse_class->get_key_purpose(s->efuse, s->efuse_block_num) == s->efuse_key_purpose ? 0 : 1;
            break;

        case A_HMAC_QUERY_BUSY_REG:
            r = 0;
            break;

        case A_HMAC_RD_RESULT_0_REG ... A_HMAC_RD_RESULT_7_REG:
            r = be32_to_cpu(s->result[(addr - A_HMAC_RD_RESULT_0_REG) / sizeof(uint32_t)]);
            break;

        default:
#if HMAC_WARNING
            /* Other registers are not supported yet */
            warn_report("[HMAC] Unsupported read to %08lx\n", addr);
#endif
            break;
    }

#if HMAC_DEBUG
    info_report("[HMAC] Reading from %08lx (%08lx)\n", addr, r);
#endif

    return r;
}


static void esp_hmac_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    ESPHmacClass *class = ESP_HMAC_GET_CLASS(opaque);
    ESPHmacState *s = ESP_HMAC(opaque);

    switch (addr) {
        case A_HMAC_SET_START_REG:
            break;

        case A_HMAC_SET_PARA_FINISH_REG:
            esp_hmac_start(s);
            break;

        case A_HMAC_SET_MESSAGE_ONE_REG:
            class->hmac_update(s, s->message);
            if(s->message_write_complete) {
                class->hmac_finish(s, s->result);
            }
            break;

        case A_HMAC_SET_MESSAGE_ING_REG:
            break;

        case A_HMAC_SET_RESULT_FINISH_REG:
            memset(s->result, 0, sizeof(s->result));
            break;

        case A_HMAC_SET_INVALIDATE_JTAG_REG:
            break;

        case A_HMAC_SET_INVALIDATE_DS_REG:
            break;

        case A_HMAC_SET_PARA_PURPOSE_REG:
            s->efuse_key_purpose = FIELD_EX32(value, HMAC_SET_PARA_PURPOSE_REG, HMAC_PURPOSE_SET);
            break;

        case A_HMAC_SET_PARA_KEY_REG:
            s->efuse_block_num = EFUSE_BLOCK_KEY0 + FIELD_EX32(value, HMAC_SET_PARA_KEY_REG, HMAC_KEY_SET);
            break;

        case A_HMAC_WR_MESSAGE_0_REG ... A_HMAC_WR_MESSAGE_15_REG:
            s->message[(addr - A_HMAC_WR_MESSAGE_0_REG) / sizeof(uint32_t)] = value;
            break;

        case A_HMAC_SET_MESSAGE_PAD_REG:
            s->message_write_complete = 1;
            break;

        case A_HMAC_ONE_BLOCK_REG:
            s->message_write_complete = 1;
            esp_hmac_finish(s, s->result);
            break;

        case A_HMAC_SET_MESSAGE_END_REG:
        case A_HMAC_SOFT_JTAG_CTRL_REG:
        case A_HMAC_WR_JTAG_REG:
        default:
#if HMAC_WARNING
            /* Other registers are not supported yet */
            warn_report("[HMAC] Unsupported write to %08lx (%08lx)\n", addr, value);
#endif
            break;
    }

#if HMAC_DEBUG
    info_report("[HMAC] Writing to %08lx (%08lx)\n", addr, value);
#endif

}


static const MemoryRegionOps esp_hmac_ops = {
    .read =  esp_hmac_read,
    .write = esp_hmac_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp_hmac_reset_hold(Object *obj, ResetType type)
{
    ESPHmacState *s = ESP_HMAC(obj);
    memset(s->message, 0, sizeof(s->message));
    memset(s->result, 0, sizeof(s->result));

    s->efuse_block_num = 0;
    s->efuse_key_purpose = 0;
    s->message_write_complete = 0;
}

static void esp_hmac_realize(DeviceState *dev, Error **errp)
{
    ESPHmacState *s = ESP_HMAC(dev);

    /* Make sure Efuse was set of issue an error */
    if (s->efuse == NULL) {
        error_report("[HMAC] Efuse controller must be set!");
    }
}

static void esp_hmac_init(Object *obj)
{
    ESPHmacState *s = ESP_HMAC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp_hmac_ops, s,
                          TYPE_ESP_HMAC, ESP_HMAC_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void esp_hmac_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ESPHmacClass* esp_hmac = ESP_HMAC_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = esp_hmac_realize;
    rc->phases.hold = esp_hmac_reset_hold;

    esp_hmac->hmac_update = esp_hmac_update;
    esp_hmac->hmac_finish = esp_hmac_finish;
}

static const TypeInfo esp_hmac_info = {
    .name = TYPE_ESP_HMAC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESPHmacState),
    .instance_init = esp_hmac_init,
    .class_init = esp_hmac_class_init,
    .class_size = sizeof(ESPHmacClass),
    .abstract = true,
};

static void esp_hmac_register_types(void)
{
    type_register_static(&esp_hmac_info);
}

type_init(esp_hmac_register_types)

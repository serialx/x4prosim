/*
 * ESP32-S3 eFuse emulation
 *
 * Copyright (c) 2024 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/nvram/esp32s3_efuse.h"


static void esp32s3_efuse_realize(DeviceState *dev, Error **errp)
{
    ESP32S3EfuseClass* esp32s3_class = ESP32S3_EFUSE_GET_CLASS(dev);

    esp32s3_class->parent_realize(dev, errp);
}


static void esp32s3_efuse_init(Object *obj)
{
}

static void esp32s3_efuse_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ESP32S3EfuseClass* esp32s3_efuse = ESP32S3_EFUSE_CLASS(klass);

    device_class_set_parent_realize(dc, esp32s3_efuse_realize, &esp32s3_efuse->parent_realize);
}

static const TypeInfo esp32s3_efuse_info = {
    .name = TYPE_ESP32S3_EFUSE,
    .parent = TYPE_ESP_EFUSE,
    .instance_size = sizeof(ESP32S3EfuseState),
    .instance_init = esp32s3_efuse_init,
    .class_init = esp32s3_efuse_class_init,
    .class_size = sizeof(ESP32S3EfuseClass)
};

static void esp32s3_efuse_register_types(void)
{
    type_register_static(&esp32s3_efuse_info);
}

type_init(esp32s3_efuse_register_types)

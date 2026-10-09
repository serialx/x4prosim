/*
 * ESP32S3 Digital Signature emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/misc/esp32s3_ds.h"

static void esp32s3_ds_class_init(ObjectClass *klass, const void *data)
{
    ESPDsClass* class = ESP_DS_CLASS(klass);
    class->mem_blk_size = ESP32S3_DS_MEM_BLK_SIZE;
    class->date = ESP32S3_DS_DATE_REG_VALUE;
}

static const TypeInfo esp32s3_ds_info = {
    .name = TYPE_ESP32S3_DS,
    .parent = TYPE_ESP_DS,
    .instance_size = sizeof(ESP32S3DsState),
    .class_init = esp32s3_ds_class_init,
    .class_size = sizeof(ESP32S3DsClass)
};

static void esp32s3_ds_register_types(void)
{
    type_register_static(&esp32s3_ds_info);
}

type_init(esp32s3_ds_register_types)

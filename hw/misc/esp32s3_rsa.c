/*
 * ESP32-S3 RSA accelerator
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/misc/esp32s3_rsa.h"

static void esp32s3_rsa_class_init(ObjectClass *klass, const void *data)
{
    ESPRsaClass* class = ESP_RSA_CLASS(klass);
    class->rsa_mem_blk_size = ESP32S3_RSA_MEM_BLK_SIZE;
    class->date = ESP32S3_RSA_DATE_REG_VALUE;
}

static const TypeInfo esp32s3_rsa_info = {
    .name = TYPE_ESP32S3_RSA,
    .parent = TYPE_ESP_RSA,
    .instance_size = sizeof(ESP32S3RsaState),
    .class_init = esp32s3_rsa_class_init,
    .class_size = sizeof(ESP32S3RsaClass)
};

static void esp32s3_rsa_register_types(void)
{
    type_register_static(&esp32s3_rsa_info);
}

type_init(esp32s3_rsa_register_types)

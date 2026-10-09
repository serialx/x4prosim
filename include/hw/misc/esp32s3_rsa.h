/*
 * ESP32-S3 RSA accelerator
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "esp_rsa.h"

#define TYPE_ESP32S3_RSA "misc.esp32s3.rsa"
#define ESP32S3_RSA(obj) OBJECT_CHECK(ESP32S3RsaState, (obj), TYPE_ESP32S3_RSA)

#define ESP32S3_RSA_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32S3RsaClass, obj, TYPE_ESP32S3_RSA)
#define ESP32S3_RSA_CLASS(klass) OBJECT_CLASS_CHECK(ESP32S3RsaClass, klass, TYPE_ESP32S3_RSA)

#define ESP32S3_RSA_MEM_BLK_SIZE    512
#define ESP32S3_RSA_DATE_REG_VALUE    0x20190425

typedef struct ESP32S3RsaState {
    ESPRsaState parent;
} ESP32S3RsaState;

typedef struct ESP32S3RsaClass {
    ESPRsaClass parent_class;
} ESP32S3RsaClass;

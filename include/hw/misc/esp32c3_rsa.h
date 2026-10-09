/*
 * ESP32-C3 RSA accelerator
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "esp_rsa.h"

#define TYPE_ESP32C3_RSA "misc.esp32c3.rsa"
#define ESP32C3_RSA(obj) OBJECT_CHECK(ESP32C3RsaState, (obj), TYPE_ESP32C3_RSA)

#define ESP32C3_RSA_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C3RsaClass, obj, TYPE_ESP32C3_RSA)
#define ESP32C3_RSA_CLASS(klass) OBJECT_CLASS_CHECK(ESP32C3RsaClass, klass, TYPE_ESP32C3_RSA)

#define ESP32C3_RSA_MEM_BLK_SIZE    384
#define ESP32C3_RSA_DATE_REG_VALUE    0x20200618

typedef struct ESP32C3RsaState {
    ESPRsaState parent;
} ESP32C3RsaState;

typedef struct ESP32C3RsaClass {
    ESPRsaClass parent_class;
} ESP32C3RsaClass;

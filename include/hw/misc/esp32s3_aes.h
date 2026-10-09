/*
 * ESP32-C3 AES emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/misc/esp_aes.h"

#define TYPE_ESP32S3_AES "misc.esp32s3.aes"
#define ESP32S3_AES(obj) OBJECT_CHECK(ESP32S3AesState, (obj), TYPE_ESP32S3_AES)

#define ESP32S3_AES_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32S3AesClass, obj, TYPE_ESP32S3_AES)
#define ESP32S3_AES_CLASS(klass) OBJECT_CLASS_CHECK(ESP32S3AesClass, klass, TYPE_ESP32S3_AES)

typedef struct ESP32S3AesState {
    ESPAesState parent;
} ESP32S3AesState;


typedef struct ESP32S3AesClass {
    ESPAesClass parent_class;
} ESP32S3AesClass;

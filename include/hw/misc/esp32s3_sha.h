/*
 * ESP32S3 SHA accelerator
 *
 * Copyright (c) 2019 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#pragma once

#include "hw/misc/esp_sha.h"

#define TYPE_ESP32S3_SHA "misc.esp32s3.sha"
#define ESP32S3_SHA(obj) OBJECT_CHECK(ESP32S3ShaState, (obj), TYPE_ESP32S3_SHA)

#define ESP32S3_SHA_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32S3ShaClass, obj, TYPE_ESP32S3_SHA)
#define ESP32S3_SHA_CLASS(klass) OBJECT_CLASS_CHECK(ESP32S3ShaClass, klass, TYPE_ESP32S3_SHA)

/**
 * @brief Size of the message array, in bytes
 */
#define ESP32S3_SHA_MESSAGE_SIZE    128
#define ESP32S3_SHA_MESSAGE_WORDS   (ESP32S3_SHA_MESSAGE_SIZE / sizeof(uint32_t))

#define ESP32S3_SHA_DATE_REG_VALUE    0x20190402

typedef struct ESP32S3ShaState {
    ESPShaState parent;
} ESP32S3ShaState;

typedef struct ESP32S3ShaClass {
    ESPShaClass parent_class;
} ESP32S3ShaClass;

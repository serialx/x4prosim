/*
 * ESP32S3 HMAC emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#pragma once

#include "hw/misc/esp_hmac.h"

#define TYPE_ESP32S3_HMAC "misc.esp32s3.hmac"
#define ESP32S3_HMAC(obj) OBJECT_CHECK(ESP32S3HmacState, (obj), TYPE_ESP32S3_HMAC)

#define ESP32S3_HMAC_DATE_REG_VALUE 0x20190402

typedef struct ESP32S3HmacState {
    ESPHmacState parent;
} ESP32S3HmacState;

typedef struct ESP32S3HmacClass {
    ESPHmacClass parent_class;
} ESP32S3HmacClass;

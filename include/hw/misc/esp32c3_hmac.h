/*
 * ESP32-C3 HMAC emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#pragma once

#include "hw/misc/esp_hmac.h"

#define TYPE_ESP32C3_HMAC "misc.esp32c3.hmac"
#define ESP32C3_HMAC(obj) OBJECT_CHECK(ESP32C3HmacState, (obj), TYPE_ESP32C3_HMAC)

#define ESP32C3_HMAC_DATE_REG_VALUE 0x20200618

typedef struct ESP32C3HmacState {
    ESPHmacState parent;
} ESP32C3HmacState;

typedef struct ESP32C3HmacClass {
    ESPHmacClass parent_class;
} ESP32C3HmacClass;

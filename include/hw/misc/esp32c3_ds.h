/*
 * ESP32-C3 Digital Signature accelerator
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#pragma once

#include "hw/misc/esp_ds.h"

#define TYPE_ESP32C3_DS "misc.esp32c3.ds"
#define ESP32C3_DS(obj) OBJECT_CHECK(ESP32C3DsState, (obj), TYPE_ESP32C3_DS)

#define ESP32C3_DS_MEM_BLK_SIZE 384
#define ESP32C3_DS_DATE_REG_VALUE 0x20200618

#define ESP32C3_DS_CIPHERTEXT_SIZE (ESP32C3_DS_MEM_BLK_SIZE + \
                                    ESP32C3_DS_MEM_BLK_SIZE + \
                                    ESP32C3_DS_MEM_BLK_SIZE + \
                                    ESP_DS_BOX_MEM_BLK_SIZE)

#define ESP32C3_DS_CALC_MD_SIZE (ESP32C3_DS_MEM_BLK_SIZE + \
                                ESP32C3_DS_MEM_BLK_SIZE + \
                                ESP32C3_DS_MEM_BLK_SIZE + \
                                ESP_DS_MPRIME_SIZE + \
                                ESP_DS_L_SIZE + \
                                ESP_DS_IV_SIZE)

typedef struct ESP32C3DsState {
    ESPDsState parent;
} ESP32C3DsState;

typedef struct ESP32C3DsClass {
    ESPDsClass parent_class;
} ESP32C3DsClass;

/*
 * ESP32-C3 TWAI (Two-Wire Automotive Interface) emulation
 *
 * Copyright (c) 2025 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#ifndef ESP32C3_TWAI_H
#define ESP32C3_TWAI_H

#include "hw/net/can/esp32_twai.h"

#define TYPE_ESP32C3_TWAI "esp32c3.twai"
#define ESP32C3_TWAI(obj) OBJECT_CHECK(Esp32C3TWAIState, (obj), TYPE_ESP32C3_TWAI)
#define ESP32C3_TWAI_CLASS(klass) OBJECT_CLASS_CHECK(Esp32C3TWAIClass, klass, TYPE_ESP32C3_TWAI)
#define ESP32C3_TWAI_GET_CLASS(obj) OBJECT_GET_CLASS(Esp32C3TWAIClass, obj, TYPE_ESP32C3_TWAI)

typedef struct Esp32C3TWAIState {
    Esp32TWAIState parent;
} Esp32C3TWAIState;

typedef struct Esp32C3TWAIClass {
    Esp32TWAIClass parent_class;

    /* Parent class method pointers for inheritance */
    DeviceRealize parent_realize;
    void (*parent_reset)(Object *obj, ResetType type);
    void (*parent_twai_write)(void *opaque, hwaddr addr, uint64_t value, unsigned int size);
    uint64_t (*parent_twai_read)(void *opaque, hwaddr addr, unsigned int size);
} Esp32C3TWAIClass;

#endif

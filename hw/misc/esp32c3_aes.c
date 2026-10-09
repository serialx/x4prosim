/*
 * ESP32C3 AES emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/misc/esp32c3_aes.h"

static const TypeInfo esp32c3_aes_info = {
        .name = TYPE_ESP32C3_AES,
        .parent = TYPE_ESP_AES,
        .instance_size = sizeof(ESP32C3AesState),
        .class_size = sizeof(ESP32C3AesClass)
};

static void esp32c3_aes_register_types(void)
{
    type_register_static(&esp32c3_aes_info);
}

type_init(esp32c3_aes_register_types)

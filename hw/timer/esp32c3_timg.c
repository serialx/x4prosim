/*
 * ESP32-C3 "Timer Group" peripheral
 *
 * Copyright (c) 2025 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/timer/esp32c3_timg.h"


static const TypeInfo esp32c3_timg_info = {
    .name = TYPE_ESP32C3_TIMG,
    .parent = TYPE_ESP_TIMG,
    .instance_size = sizeof(ESP32C3TimgState),
    .class_size = sizeof(ESP32C3TimgClass),
};

static void esp32c3_timg_register_types(void)
{
    type_register_static(&esp32c3_timg_info);
}

type_init(esp32c3_timg_register_types)

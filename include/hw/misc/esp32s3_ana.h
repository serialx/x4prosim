#pragma once

#include "hw/core/sysbus.h"


#define TYPE_ESP32S3_ANA "misc.esp32s3.ana"
#define ESP32S3_ANA(obj) OBJECT_CHECK(Esp32S3AnaState, (obj), TYPE_ESP32S3_ANA)

typedef struct Esp32S3AnaState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t mem[1024];
} Esp32S3AnaState;

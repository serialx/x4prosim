#pragma once

#include "hw/core/sysbus.h"
#include "hw/core/registerfields.h"

#define TYPE_ESP32S3_GPIO "esp32s3.gpio"
#define ESP32S3_GPIO(obj)           OBJECT_CHECK(ESP32S3GPIOState, (obj), TYPE_ESP32S3_GPIO)

/* Bootstrap options for ESP32-S3 (4-bit) */
#define ESP32S3_STRAP_MODE_FLASH_BOOT 0x4   /* SPI Boot */

#define ESP32S3_GPIO_COUNT 49

/* Named GPIO lines: "pin-in" (external level into pin n), "pin-out" (driven level of pin n). */
#define ESP32S3_GPIO_IN  "pin-in"
#define ESP32S3_GPIO_OUT "pin-out"
/* Named GPIO out to the RTC controller for light-sleep GPIO wakeup. */
#define ESP32S3_GPIO_WAKE "wake"

typedef struct ESP32S3State {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;
    qemu_irq out[ESP32S3_GPIO_COUNT];
    qemu_irq wake;          /* a pin with wakeup enabled is at its trigger level */
    uint32_t strap_mode;
    uint64_t out_reg;
    uint64_t enable;
    uint64_t status;
    uint64_t ext_level;     /* level applied from outside */
    uint64_t ext_driven;    /* pins something outside drives */
    uint64_t last_in;
    uint64_t last_out;      /* last level sent on pin-out lines */
    uint32_t pin[ESP32S3_GPIO_COUNT];
} ESP32S3GPIOState;

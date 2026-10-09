#pragma once

#include "hw/timer/esp_timg.h"


#define TYPE_ESP32C3_TIMG           "timer.esp32c3.timg"
#define ESP32C3_TIMG(obj)           OBJECT_CHECK(ESP32C3TimgState, (obj), TYPE_ESP32C3_TIMG)
#define ESP32C3_TIMG_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32C3TimgClass, obj, TYPE_ESP32C3_TIMG)
#define ESP32C3_TIMG_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32C3TimgClass, klass, TYPE_ESP32C3_TIMG)


#define ESP32C3_T0_IRQ_INTERRUPT        ESP_T0_IRQ_INTERRUPT
#define ESP32C3_T1_IRQ_INTERRUPT        ESP_T1_IRQ_INTERRUPT
#define ESP32C3_WDT_IRQ_INTERRUPT       ESP_WDT_IRQ_INTERRUPT
#define ESP32C3_WDT_IRQ_RESET           ESP_WDT_IRQ_RESET


typedef ESPTimgState ESP32C3TimgState;
typedef ESPTimgClass ESP32C3TimgClass;

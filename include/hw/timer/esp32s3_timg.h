#pragma once

#include "hw/timer/esp_timg.h"


#define TYPE_ESP32S3_TIMG           "timer.esp32s3.timg"
#define ESP32S3_TIMG(obj)           OBJECT_CHECK(ESP32S3TimgState, (obj), TYPE_ESP32S3_TIMG)
#define ESP32S3_TIMG_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32S3TimgClass, obj, TYPE_ESP32S3_TIMG)
#define ESP32S3_TIMG_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32S3TimgClass, klass, TYPE_ESP32S3_TIMG)


#define ESP32S3_T0_IRQ_INTERRUPT        ESP_T0_IRQ_INTERRUPT
#define ESP32S3_T1_IRQ_INTERRUPT        ESP_T1_IRQ_INTERRUPT
#define ESP32S3_WDT_IRQ_INTERRUPT       ESP_WDT_IRQ_INTERRUPT
#define ESP32S3_WDT_IRQ_RESET           ESP_WDT_IRQ_RESET


typedef ESPTimgState ESP32S3TimgState;
typedef ESPTimgClass ESP32S3TimgClass;

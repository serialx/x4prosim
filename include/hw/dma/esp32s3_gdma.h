/*
 * ESP32-S3 GDMA emulation
 *
 * Copyright (c) 2025 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/dma/esp_gdma.h"

#define TYPE_ESP32S3_GDMA "esp32s3.gdma"

#define ESP32S3_GDMA(obj)              OBJECT_CHECK(ESP32S3GdmaState, (obj), TYPE_ESP32S3_GDMA)
#define ESP32S3_GDMA_GET_CLASS(obj)    OBJECT_GET_CLASS(ESP32S3GdmaClass, obj, TYPE_ESP32S3_GDMA)
#define ESP32S3_GDMA_CLASS(klass)      OBJECT_CLASS_CHECK(ESP32S3GdmaClass, klass, TYPE_ESP32S3_GDMA)


#define ESP32S3_GDMA_CHANNEL_COUNT     5

/**
 * @brief Size of the I/O memory region for the GDMA component
 */
#define ESP32S3_GDMA_REGS_SIZE         (A_DMA_DATE + 4)


typedef struct ESP32S3GdmaState {
    ESPGdmaState parent;

    MemoryRegion iomem;
} ESP32S3GdmaState;


typedef struct ESP32S3GdmaClass {
    ESPGdmaClass parent_class;
} ESP32S3GdmaClass;


/**
 * @brief Each of the 5 DMA channels available are organized as a set of 13 configuration regiters.
 * We don't need to define an address for all the channel, we can describe a single one, only the
 * starting offset is necessary for each channel.
 */

#define DMA_DIR_REGS_SIZE   (0x60)   // Each direction has 0x60 bytes of registers
#define DMA_CHAN_REGS_SIZE  (DMA_DIR_REGS_SIZE * 2)   // Each channel has 2 directions


REG32(DMA_CONF0,         0x000)
REG32(DMA_CONF1,         0x004)
REG32(DMA_INT_RAW,       0x008)
REG32(DMA_INT_ST,        0x00C)
REG32(DMA_INT_ENA,       0x010)
REG32(DMA_INT_CLR,       0x014)
REG32(DMA_FIFO_ST,       0x018)
REG32(DMA_POP,           0x01C)
REG32(DMA_LINK,          0x020)
REG32(DMA_STATE,         0x024)
REG32(DMA_SUC_EOF_DESC,  0x028)
REG32(DMA_ERR_EOF_DESC,  0x02C)
REG32(DMA_DESC_ADDR,     0x030)
REG32(DMA_BF0_DESC_ADDR, 0x034)
REG32(DMA_BF1_DESC_ADDR, 0x038)
REG32(DMA_DUMMY_3C,      0x03c)
REG32(DMA_DUMMY_40,      0x040)
REG32(DMA_PRIORITY,      0x044)
REG32(DMA_PERI_SEL,      0x048)

/**
 * @brief Bitfields for the interrupt registers
 */
FIELD(DMA_INT_RAW, INFIFO_FULL_WM, 5, 1)
FIELD(DMA_INT_RAW, IN_DSCR_EMPTY,  4, 1)
FIELD(DMA_INT_RAW, IN_DSCR_ERR,    3, 1)
FIELD(DMA_INT_RAW, IN_ERR_EOF,     2, 1)
FIELD(DMA_INT_RAW, IN_SUC_EOF,     1, 1)
FIELD(DMA_INT_RAW, IN_DONE,        0, 1)

FIELD(DMA_INT_RAW, OUT_TOTAL_EOF,  3, 1)
FIELD(DMA_INT_RAW, OUT_DSCR_ERR,   2, 1)
FIELD(DMA_INT_RAW, OUT_EOF,        1, 1)
FIELD(DMA_INT_RAW, OUT_DONE,       0, 1)


/**
 * @brief Miscellaneous registers
 */
REG32(DMA_MISC_CONF, 0x3c8)

REG32(DMA_DATE, 0x40c)

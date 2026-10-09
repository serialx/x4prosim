/*
 * ESP32-C3 GDMA emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#pragma once

#include "hw/core/sysbus.h"
#include "hw/dma/esp_gdma.h"
#include "hw/core/registerfields.h"

#define TYPE_ESP32C3_GDMA "esp32c3.gdma"

#define ESP32C3_GDMA(obj)              OBJECT_CHECK(ESP32C3GdmaState, (obj), TYPE_ESP32C3_GDMA)
#define ESP32C3_GDMA_GET_CLASS(obj)    OBJECT_GET_CLASS(ESP32C3GdmaClass, obj, TYPE_ESP32C3_GDMA)
#define ESP32C3_GDMA_CLASS(klass)      OBJECT_CLASS_CHECK(ESP32C3GdmaClass, klass, TYPE_ESP32C3_GDMA)


#define ESP32C3_GDMA_CHANNEL_COUNT 3


/**
 * @brief Size of the I/O memory region for the GDMA component
 */
#define ESP32C3_GDMA_REGS_SIZE         (A_DMA_OUT_PERI_SEL_CH2 + 4)


typedef struct ESP32C3GdmaState {
    ESPGdmaState parent;

    MemoryRegion iomem;
} ESP32C3GdmaState;


typedef struct ESP32C3GdmaClass {
    ESPGdmaClass parent_class;
} ESP32C3GdmaClass;


#define DMA_DIR_REGS_SIZE   (0x60)   // Each direction has 0x60 bytes of registers
#define DMA_CHAN_REGS_START (0x70)
#define DMA_CHAN_REGS_END   (0x0280)
#define DMA_CHAN_REGS_SIZE  (DMA_DIR_REGS_SIZE * 2)   // Each channel has 2 directions


REG32(DMA_CONF0,         0x000)
REG32(DMA_CONF1,         0x004)
REG32(DMA_FIFO_ST,       0x008)
REG32(DMA_POP,           0x00C)
REG32(DMA_LINK,          0x010)
REG32(DMA_STATE,         0x014)
REG32(DMA_SUC_EOF_DESC,  0x018)
REG32(DMA_ERR_EOF_DESC,  0x01C)
REG32(DMA_DESC_ADDR,     0x020)
REG32(DMA_BF0_DESC_ADDR, 0x024)
REG32(DMA_BF1_DESC_ADDR, 0x028)
REG32(DMA_PRIORITY,      0x02C)
REG32(DMA_PERI_SEL,      0x030)

/* The interrupt registers take 16 bytes per channel */
#define DMA_INT_CHAN_REGS_SIZE      (0x10)
/* Offset where the interrupt registers start and end respectively */
#define DMA_INT_CHAN_REGS_START     (0x00)
#define DMA_INT_CHAN_REGS_END       (0x2c)

REG32(DMA_INT_RAW, 0x000)
    FIELD(DMA_INT_RAW, OUTFIFO_UDF,   12, 1)
    FIELD(DMA_INT_RAW, OUTFIFO_OVF,   11, 1)
    FIELD(DMA_INT_RAW, INFIFO_UDF,    10, 1)
    FIELD(DMA_INT_RAW, INFIFO_OVF,    9, 1)
    FIELD(DMA_INT_RAW, OUT_TOTAL_EOF, 8, 1)
    FIELD(DMA_INT_RAW, IN_DSCR_EMPTY, 7, 1)
    FIELD(DMA_INT_RAW, OUT_DSCR_ERR,  6, 1)
    FIELD(DMA_INT_RAW, IN_DSCR_ERR,   5, 1)
    FIELD(DMA_INT_RAW, OUT_EOF,       4, 1)
    FIELD(DMA_INT_RAW, OUT_DONE,      3, 1)
    FIELD(DMA_INT_RAW, IN_ERR_EOF,    2, 1)
    FIELD(DMA_INT_RAW, IN_SUC_EOF,    1, 1)
    FIELD(DMA_INT_RAW, IN_DONE,       0, 1)
REG32(DMA_INT_ST,  0x004)
REG32(DMA_INT_ENA, 0x008)
REG32(DMA_INT_CLR, 0x00C)



REG32(DMA_OUT_PERI_SEL_CH2, 0x280)

REG32(DMA_MISC_CONF, 0x044)

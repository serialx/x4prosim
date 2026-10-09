/*
 * RSA accelerator emulation for recent ESP32-series chip (ESP32-S3 and newer)
 *
 * Copyright (c) 2024 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "hw/core/sysbus.h"
#include "hw/core/registerfields.h"


#define TYPE_ESP_RSA "misc.esp.rsa"
#define ESP_RSA(obj) OBJECT_CHECK(ESPRsaState, (obj), TYPE_ESP_RSA)

#define ESP_RSA_GET_CLASS(obj) OBJECT_GET_CLASS(ESPRsaClass, obj, TYPE_ESP_RSA)
#define ESP_RSA_CLASS(klass) OBJECT_CLASS_CHECK(ESPRsaClass, klass, TYPE_ESP_RSA)

#define ESP_RSA_MAX_MEM_BLK_SIZE    512

typedef struct ESPRsaState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    uint32_t m_mem[ESP_RSA_MAX_MEM_BLK_SIZE / 4];
    uint32_t z_mem[ESP_RSA_MAX_MEM_BLK_SIZE / 4];
    uint32_t y_mem[ESP_RSA_MAX_MEM_BLK_SIZE / 4];
    uint32_t x_mem[ESP_RSA_MAX_MEM_BLK_SIZE / 4];

    /* Configuration registers */
    uint32_t mprime_reg;
    uint32_t mode_reg;
    uint32_t const_time_reg;
    uint32_t search_ena_reg;
    uint32_t search_pos_reg;

    /* Status/Control registers */
    uint32_t int_ena;
    qemu_irq irq;
} ESPRsaState;

typedef struct ESPRsaClass {
    SysBusDeviceClass parent_class;

    /* Target specific registers */
    uint32_t rsa_mem_blk_size;
    /* Version register */
    uint32_t date;

    /* Virtual methods*/
    void (*rsa_exp_mod)(ESPRsaState *s, uint32_t mode_reg, uint32_t *x_mem, uint32_t *y_mem, uint32_t *m_mem, uint32_t *z_mem, uint32_t int_ena);
} ESPRsaClass;


REG32(RSA_MEM_M_BLOCK_BASE, 0x000)

REG32(RSA_MEM_Z_BLOCK_BASE, 0x200)

REG32(RSA_MEM_Y_BLOCK_BASE, 0x400)

REG32(RSA_MEM_X_BLOCK_BASE, 0x600)

REG32(RSA_M_PRIME_REG, 0x800)

REG32(RSA_MODE_REG, 0x804)
    FIELD(RSA_MODE_REG, RSA_MODE, 0, 7)

REG32(RSA_CLEAN_REG, 0x808)
    FIELD(RSA_CLEAN_REG, RSA_CLEAN, 0, 1)

REG32(RSA_MODEXP_START_REG, 0x80C)
    FIELD(RSA_MODEXP_START_REG, RSA_MODEXP_START, 0, 1)

REG32(RSA_MODMULT_START_REG, 0x810)
    FIELD(RSA_MODMULT_START_REG, RSA_MODMULT_START, 0, 1)

REG32(RSA_MULT_START_REG, 0x814)
    FIELD(RSA_MULT_START_REG, RSA_MULT_START, 0, 1)

REG32(RSA_IDLE_REG, 0x818)
    FIELD(RSA_IDLE_REG, RSA_IDLE, 0, 1)

REG32(RSA_CLEAR_INTERRUPT_REG, 0x81C)
    FIELD(RSA_CLEAR_INTERRUPT_REG, RSA_CLEAR_INTERRUPT, 0, 1)

REG32(RSA_CONSTANT_TIME_REG, 0x820)
    FIELD(RSA_CONSTANT_TIME_REG, RSA_CONSTANT_TIME, 0, 1)

REG32(RSA_SEARCH_ENABLE_REG, 0x824)
    FIELD(RSA_SEARCH_ENABLE_REG, RSA_SEARCH_ENABLE, 0, 1)

REG32(RSA_SEARCH_POS_REG, 0x828)
    FIELD(RSA_SEARCH_POS_REG, RSA_SEARCH_POS, 0, 12)

REG32(RSA_INTERRUPT_ENA_REG, 0x82C)
    FIELD(RSA_INTERRUPT_ENA_REG, RSA_INTERRUPT_ENA, 0, 1)

REG32(RSA_DATE_REG, 0x830)

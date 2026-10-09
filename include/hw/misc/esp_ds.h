/*
 * ESP Digital Signature accelerator
 *
 * Copyright (c) 2025 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#pragma once

#include "hw/core/sysbus.h"
#include "hw/core/registerfields.h"
#include "hw/misc/esp_aes.h"
#include "hw/misc/esp_sha.h"
#include "hw/misc/esp_rsa.h"
#include "hw/misc/esp_hmac.h"

#define TYPE_ESP_DS "misc.esp.ds"
#define ESP_DS(obj) OBJECT_CHECK(ESPDsState, (obj), TYPE_ESP_DS)

#define ESP_DS_GET_CLASS(obj) OBJECT_GET_CLASS(ESPDsClass, obj, TYPE_ESP_DS)
#define ESP_DS_CLASS(klass) OBJECT_CLASS_CHECK(ESPDsClass, klass, TYPE_ESP_DS)

#define ESP_DS_KEY_SIZE 32

#define ESP_DS_REGS_SIZE (A_DS_DATE_REG + 4)
#define ESP_DS_MAX_MEM_BLK_SIZE 512
#define ESP_DS_BOX_MEM_BLK_SIZE 48
#define ESP_DS_IV_SIZE 16
#define ESP_DS_MPRIME_SIZE 4
#define ESP_DS_L_SIZE 4
#define ESP_DS_MD_SIZE 32

typedef enum {
    DS_SIGNATURE_OK = 0,                    /**< Signature is valid and can be read. */
    DS_SIGNATURE_MD_FAIL = 1,               /**< Message digest check failed, signature invalid. */
    DS_SIGNATURE_PADDING_FAIL = 2,          /**< Padding invalid, signature can be read if user wants it. */
    DS_SIGNATURE_PADDING_AND_MD_FAIL = 3,   /**< Both padding and MD check failed. */
} EspDsSignatureCheck;

typedef struct ESPDsState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    uint32_t *y_mem;
    uint32_t *m_mem;
    uint32_t *rb_mem;
    uint32_t *box_mem;
    uint32_t *x_mem;
    uint32_t *z_mem;
    uint32_t iv[ESP_DS_IV_SIZE / 4];

    uint32_t ds_key[ESP_DS_KEY_SIZE / 4];
    EspDsSignatureCheck ds_signature_check;

    ESPHmacState *hmac;
    ESPAesState *aes;
    ESPRsaState *rsa;
    ESPShaState *sha;
} ESPDsState;

typedef struct ESPDsClass {
    SysBusDeviceClass parent_class;

    /* Target-specific registers */
    uint32_t mem_blk_size;
    /* peripheral version register (date) */
    uint32_t date;
} ESPDsClass;

REG32(DS_MEM_Y_BLOCK_BASE, 0x0000)
REG32(DS_MEM_M_BLOCK_BASE, 0x0200)
REG32(DS_MEM_RB_BLOCK_BASE, 0x0400)
REG32(DS_MEM_BOX_BLOCK_BASE, 0x0600)
REG32(DS_MEM_X_BLOCK_BASE, 0x0800)
REG32(DS_MEM_Z_BLOCK_BASE, 0x0A00)

REG32(DS_IV_0_REG, 0x0630)
REG32(DS_IV_1_REG, 0x0634)
REG32(DS_IV_2_REG, 0x0638)
REG32(DS_IV_3_REG, 0x063C)

REG32(DS_SET_START_REG, 0xE00)
    FIELD(DS_SET_START_REG, DS_SET_START, 0, 1)

REG32(DS_SET_ME_REG, 0xE04)
    FIELD(DS_SET_ME_REG, DS_SET_ME, 0, 1)

REG32(DS_SET_FINISH_REG, 0xE08)
    FIELD(DS_SET_FINISH_REG, DS_SET_FINISH, 0, 1)

REG32(DS_QUERY_BUSY_REG, 0xE0C)
    FIELD(DS_QUERY_BUSY_REG, DS_QUERY_BUSY, 0, 1)

REG32(DS_QUERY_KEY_WRONG_REG, 0xE10)
    FIELD(DS_QUERY_KEY_WRONG_REG, DS_QUERY_KEY_WRONG, 0, 4)

REG32(DS_QUERY_CHECK_REG, 0xE14)
    FIELD(DS_QUERY_CHECK_REG, DS_MD_ERROR, 0, 1)
    FIELD(DS_QUERY_CHECK_REG, DS_PADDING_BAD, 1, 1)

REG32(DS_DATE_REG, 0xE20)
    FIELD(DS_DATE_REG, DS_DATE, 0, 30)

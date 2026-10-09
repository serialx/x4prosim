/*
 * ESP SHA emulation
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
#include "crypto/sha512_t_i.h"
#include "crypto/sha512_256_i.h"
#include "crypto/sha512_224_i.h"
#include "crypto/sha512_i.h"
#include "crypto/sha384_i.h"
#include "crypto/sha256_i.h"
#include "crypto/sha224_i.h"
#include "crypto/sha1_i.h"

#include "hw/dma/esp_gdma.h"

#define TYPE_ESP_SHA "misc.esp.sha"
#define ESP_SHA(obj) OBJECT_CHECK(ESPShaState, (obj), TYPE_ESP_SHA)

#define ESP_SHA_GET_CLASS(obj) OBJECT_GET_CLASS(ESPShaClass, obj, TYPE_ESP_SHA)
#define ESP_SHA_CLASS(klass) OBJECT_CLASS_CHECK(ESPShaClass, klass, TYPE_ESP_SHA)

#define ESP_SHA_REGS_SIZE (0x100)


/**
 * @brief Size of the message array, in bytes
 */
#define ESP_SHA_MAX_MESSAGE_SIZE    128
#define ESP_SHA_MAX_MESSAGE_WORDS   (ESP_SHA_MAX_MESSAGE_SIZE / sizeof(uint32_t))


/**
 * @brief Mode configuration for the SHA_MODE register.
 */
typedef enum {
    ESP_SHA_1_MODE   = 0,
    ESP_SHA_224_MODE = 1,
    ESP_SHA_256_MODE = 2,
    ESP_SHA_384_MODE = 3,
    ESP_SHA_512_MODE = 4,
    ESP_SHA_512_224_MODE = 5,
    ESP_SHA_512_256_MODE = 6,
    ESP_SHA_512_t_MODE = 7,
} ESPShaMode;


#define SHA_OP_TYPE_MASK    (1 << 0)
#define SHA_OP_DMA_MASK     (1 << 1)

typedef enum {
    OP_START         = 0,
    OP_CONTINUE      = 1,
    OP_DMA_START     = SHA_OP_DMA_MASK | OP_START,
    OP_DMA_CONTINUE  = SHA_OP_DMA_MASK | OP_CONTINUE,
} ESPShaOperation;

typedef union {
    struct sha512_state sha512;
    struct sha256_state sha256;
    struct sha1_state   sha1;
} ESPHashContext;


typedef void (*hash_init)(void *);
typedef void (*hash_init_message)(uint32_t *, size_t, uint32_t, uint32_t);
typedef void (*hash_compress)(void *, const uint8_t*);

typedef struct {
    hash_init init;
    hash_init_message init_message;
    /* For all types of hash, the message to "compress" must be 64-byte long (16 words of 32 bits) */
    hash_compress compress;
    /* Length of the context in bytes */
    size_t len;
} ESPHashAlg;


typedef struct ESPShaState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    /* SHA mode selected by the application */
    ESPShaMode mode;
    /* Context for the hash calculation */
    ESPHashContext context;

    /* Resulted hash value */
    uint32_t hash[16];
    /* User data value */
    uint32_t message[ESP_SHA_MAX_MESSAGE_WORDS];

    uint32_t t;
    uint32_t t_len;

    /* DMA related */
    /* Number of block to process in DMA mode */
    uint32_t block;
    bool int_ena;
    qemu_irq irq;

    /* Public: must be set before realizing instance*/
    ESPGdmaState *gdma;
} ESPShaState;

typedef struct ESPShaClass {
    SysBusDeviceClass parent_class;

    /* Target specific registers */
    uint32_t message_len;
    /* Version register */
    uint32_t date;

    /* Virtual methods*/
    void (*sha_start)(ESPShaState *s, ESPShaOperation op, uint32_t mode, uint32_t *message, uint32_t *hash);
} ESPShaClass;


REG32(SHA_MODE, 0x000)
    FIELD(SHA_MODE, MODE, 0, 3)

REG32(SHA_T_STRING, 0x004)

REG32(SHA_T_LENGTH, 0x008)
    FIELD(SHA_T_LENGTH, T_LENGTH, 0, 7)

REG32(SHA_DMA_BLOCK_NUM, 0x00C)
    FIELD(SHA_DMA_BLOCK_NUM, DMA_BLOCK_NUM, 0, 6)

REG32(SHA_START, 0x010)
    FIELD(SHA_START, START, 0, 1)

REG32(SHA_CONTINUE, 0x014)
    FIELD(SHA_CONTINUE, CONTINUE, 0, 1)

REG32(SHA_BUSY, 0x018)
    FIELD(SHA_BUSY, BUSY_STATE, 0, 1)

REG32(SHA_DMA_START, 0x01C)
    FIELD(SHA_DMA_START, DMA_START, 0, 1)

REG32(SHA_DMA_CONTINUE, 0x020)
    FIELD(SHA_DMA_CONTINUE, DMA_CONTINUE, 0, 1)

REG32(SHA_CLEAR_IRQ, 0x024)
    FIELD(SHA_CLEAR_IRQ, CLEAR_INTERRUPT, 0, 1)

REG32(SHA_IRQ_ENA, 0x028)
    FIELD(SHA_IRQ_ENA, INTERRUPT_ENA, 0, 1)

REG32(SHA_DATE, 0x02C)
    FIELD(SHA_DATE, DATE, 0, 30)

REG32(SHA_H_MEM, 0x040)

REG32(SHA_M_MEM, 0x080)

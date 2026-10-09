/*
 * ESP Digital Signature emulation
 *
 * Copyright (c) 2025 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "hw/misc/esp_ds.h"

#define DS_WARNING 0
#define DS_DEBUG   0

static void write_and_padd(uint8_t *block, const uint8_t *data, uint16_t data_len)
{
    memcpy(block, data, data_len);
    // Apply a one bit, followed by zero bits (refer to the TRM of respective target).
    block[data_len] = 0x80;
    memset(block + data_len + 1, 0, SHA256_BLOCK_SIZE - data_len - 1);
}

static void esp_ds_generate_ds_key(ESPDsState *s)
{
    uint8_t ones[ESP_DS_KEY_SIZE];
    memset(ones, 0xFF, ESP_DS_KEY_SIZE);

    uint8_t block[SHA256_BLOCK_SIZE];
    uint64_t bit_len = be64_to_cpu(sizeof(ones) * 8 + 512);

    write_and_padd(block, ones, sizeof(ones));
    memcpy(block + SHA256_BLOCK_SIZE - sizeof(bit_len), &bit_len, sizeof(bit_len));

    ESPHmacClass *hmac_class = ESP_HMAC_GET_CLASS(s->hmac);

    hmac_class->hmac_update(s->hmac, (uint32_t*) block);
    hmac_class->hmac_finish(s->hmac, s->ds_key);

    for(int i = 0; i < ESP_DS_KEY_SIZE / 4; i++) {
        s->ds_key[i] = be32_to_cpu(s->ds_key[i]);
    }

    // The DS peripheral resets the HMAC peripheral once it has completed the respective operation.
    resettable_reset(OBJECT(s->hmac), RESET_TYPE_S390_CPU_NORMAL);
}

static void esp_ds_decrypt_ciphertext(ESPDsState *s, uint8_t *buffer)
{
    ESPAesClass *aes_class = ESP_AES_GET_CLASS(s->aes);
    ESPDsClass *class = ESP_DS_GET_CLASS(s);

    uint32_t *output_words = (uint32_t *)buffer;
    const uint32_t *input_words = (const uint32_t *)buffer;

    uint32_t iv_words[ESP_DS_IV_SIZE / 4];
    memcpy(iv_words, s->iv, ESP_DS_IV_SIZE);

    unsigned char temp[16];
    uint32_t length = class->mem_blk_size * 3 + ESP_DS_BOX_MEM_BLK_SIZE;

    while (length > 0) {
        memcpy(temp, input_words, 16);

        aes_class->aes_block_start(s->aes, s->ds_key, input_words, output_words, ESP_AES_MODE_256_DEC);

        output_words[0] = output_words[0] ^ iv_words[0];
        output_words[1] = output_words[1] ^ iv_words[1];
        output_words[2] = output_words[2] ^ iv_words[2];
        output_words[3] = output_words[3] ^ iv_words[3];

        memcpy(iv_words, temp, 16);

        input_words += 4;
        output_words += 4;
        length -= 16;
    }

    // The DS peripheral resets the AES peripheral once it has completed the respective operation.
    resettable_reset(OBJECT(s->aes), RESET_TYPE_S390_CPU_NORMAL);
}

static bool md_and_pad_check(ESPDsState *s)
{
    /* parse box */
    int index = 0;
    bool ret = false;

    /* md */
    uint8_t md[ESP_DS_MD_SIZE];
    memcpy(md, s->box_mem + index, ESP_DS_MD_SIZE);
    index += ESP_DS_MD_SIZE / 4;

    /* mprime */
    uint8_t mprime[ESP_DS_MPRIME_SIZE];
    memcpy(mprime, s->box_mem + index, ESP_DS_MPRIME_SIZE);
    index += ESP_DS_MPRIME_SIZE / 4;

    /* l */
    uint8_t l[ESP_DS_L_SIZE];
    memcpy(l, s->box_mem + index, ESP_DS_L_SIZE);
    index += ESP_DS_L_SIZE / 4;

    /* beta */
    uint8_t beta[8];
    memcpy(beta, s->box_mem + index, 8);

    /* Padding check */
    uint8_t beta_pkcs7[8];
    memset(beta_pkcs7, 8, sizeof(beta_pkcs7));

    s->ds_signature_check = DS_SIGNATURE_PADDING_AND_MD_FAIL;

    if (memcmp(beta_pkcs7, beta, sizeof(beta_pkcs7)) == 0) {
        s->ds_signature_check ^= DS_SIGNATURE_PADDING_FAIL;
    } else {
        error_report("[Digital Signature] Invalid padding");
    }

    /* MD check */
    uint32_t md_check[ESP_DS_MD_SIZE / 4];
    ESPShaClass *sha_class = ESP_SHA_GET_CLASS(s->sha);
    ESPDsClass *class = ESP_DS_GET_CLASS(s);

    size_t calc_md_size = class->mem_blk_size * 3 + ESP_DS_MPRIME_SIZE + ESP_DS_L_SIZE + ESP_DS_IV_SIZE;
    uint8_t *buffer = g_malloc(calc_md_size);
    if (buffer == NULL) {
        error_report("[Digital Signature] No more memory in host!");
        return false;
    }

    index = 0;

    memcpy(buffer + index, s->y_mem, class->mem_blk_size);
    index += class->mem_blk_size;

    memcpy(buffer + index, s->m_mem, class->mem_blk_size);
    index += class->mem_blk_size;

    memcpy(buffer + index, s->rb_mem, class->mem_blk_size);
    index += class->mem_blk_size;

    memcpy(buffer + index, mprime, ESP_DS_MPRIME_SIZE);
    index += ESP_DS_MPRIME_SIZE;

    memcpy(buffer + index, l, ESP_DS_L_SIZE);
    index += ESP_DS_L_SIZE;

    memcpy(buffer + index, s->iv, ESP_DS_IV_SIZE);

    size_t remaining_blocks = calc_md_size / SHA256_BLOCK_SIZE;

    for (int i = 0; i < remaining_blocks; i++) {
        if (i == 0) {
            sha_class->sha_start(s->sha, OP_START, ESP_SHA_256_MODE, (uint32_t*) (buffer + i * SHA256_BLOCK_SIZE), md_check);
        } else {
            sha_class->sha_start(s->sha, OP_CONTINUE, ESP_SHA_256_MODE, (uint32_t*) (buffer + i * SHA256_BLOCK_SIZE), md_check);
        }
    }

    size_t remaining = calc_md_size % SHA256_BLOCK_SIZE;
    if (remaining != 0) {
        uint8_t block[SHA256_BLOCK_SIZE];
        uint64_t bit_len = be64_to_cpu(calc_md_size * 8);
        write_and_padd(block, buffer + calc_md_size - remaining, remaining);
        memcpy(block + SHA256_BLOCK_SIZE - sizeof(bit_len), &bit_len, sizeof(bit_len));
        sha_class->sha_start(s->sha, OP_CONTINUE, ESP_SHA_256_MODE, (uint32_t*) block, md_check);
    }

    g_free(buffer);

    for (int i = 0; i < SHA256_DIGEST_SIZE / 4; i++) {
        md_check[i] = be32_to_cpu(md_check[i]);
    }

    // The DS peripheral resets the SHA peripheral once it has completed the respective operation.
    resettable_reset(OBJECT(s->sha), RESET_TYPE_S390_CPU_NORMAL);

    if (memcmp(md, md_check, SHA256_DIGEST_SIZE) == 0) {
        s->ds_signature_check ^= DS_SIGNATURE_MD_FAIL;
        ret = true;
    } else {
        error_report("[Digital Signature] Invalid digest");
    }
    return ret;
}

static void esp_ds_generate_signature(ESPDsState *s)
{
    ESPDsClass *class = ESP_DS_GET_CLASS(s);
    uint32_t mode = class->mem_blk_size / 4 - 1;

    ESPRsaClass *rsa_class = ESP_RSA_GET_CLASS(s->rsa);
    rsa_class->rsa_exp_mod(s->rsa, mode, s->x_mem, s->y_mem, s->m_mem, s->z_mem, 0);

    // The DS peripheral resets the RSA peripheral once it has completed the respective operation.
    resettable_reset(OBJECT(s->rsa), RESET_TYPE_S390_CPU_NORMAL);
}

static void esp_ds_calculate(ESPDsState *s)
{
    ESPDsClass *class = ESP_DS_GET_CLASS(s);
    /* Re-Generate the plaintext from the ciphertext */
    size_t ciphertext_size = class->mem_blk_size * 3 + ESP_DS_BOX_MEM_BLK_SIZE;
    uint8_t *buffer = g_malloc(ciphertext_size);
    if (buffer == NULL) {
        error_report("[Digital Signature] No more memory in host!");
        return;
    }

    int index = 0;

    /* Y */
    memcpy(buffer + index, s->y_mem, class->mem_blk_size);
    index += class->mem_blk_size;

    /* M */
    memcpy(buffer + index, s->m_mem, class->mem_blk_size);
    index += class->mem_blk_size;

    /* rb */
    memcpy(buffer + index, s->rb_mem, class->mem_blk_size);
    index += class->mem_blk_size;

    /* box */
    memcpy(buffer + index, s->box_mem, ESP_DS_BOX_MEM_BLK_SIZE);

    /* Decrypt ciphertext */
    esp_ds_decrypt_ciphertext(s, buffer);

    /* Parse params */
    /* Y */
    index = 0;
    memcpy(s->y_mem, buffer + index, class->mem_blk_size);
    index += class->mem_blk_size;

    /* M */
    memcpy(s->m_mem, buffer + index, class->mem_blk_size);
    index += class->mem_blk_size;

    /* rb */
    memcpy(s->rb_mem, buffer + index, class->mem_blk_size);
    index += class->mem_blk_size;

    /* box */
    memcpy(s->box_mem, buffer + index, ESP_DS_BOX_MEM_BLK_SIZE);

    g_free(buffer);

    if (!md_and_pad_check(s)) {
        return;
    }

    /* Generate signature */
    esp_ds_generate_signature(s);
}

static void esp_ds_clear_buffers(ESPDsState *s)
{
    ESPDsClass *class = ESP_DS_GET_CLASS(s);

    memset(s->y_mem, 0, class->mem_blk_size);
    memset(s->m_mem, 0, class->mem_blk_size);
    memset(s->rb_mem, 0, class->mem_blk_size);
    memset(s->box_mem, 0, ESP_DS_BOX_MEM_BLK_SIZE);
    memset(s->x_mem, 0, class->mem_blk_size);
    memset(s->z_mem, 0, class->mem_blk_size);
    memset(s->iv, 0, ESP_DS_IV_SIZE);
    memset(s->ds_key, 0, ESP_DS_KEY_SIZE);
}

static uint64_t esp_ds_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESPDsState *s = ESP_DS(opaque);
    ESPDsClass *class = ESP_DS_GET_CLASS(opaque);

    uint64_t r = 0;
    switch (addr) {
        case A_DS_QUERY_BUSY_REG:
            r = 0;
            break;

        case A_DS_QUERY_CHECK_REG:
            r = s->ds_signature_check;
            break;

        case A_DS_DATE_REG:
            r = class->date;
            break;

        case A_DS_MEM_Z_BLOCK_BASE ... (A_DS_MEM_Z_BLOCK_BASE + ESP_DS_MAX_MEM_BLK_SIZE - 1):
            r = s->z_mem[(addr - A_DS_MEM_Z_BLOCK_BASE) / sizeof(uint32_t)];
            break;

        case A_DS_QUERY_KEY_WRONG_REG:
        default:
#if DS_WARNING
            /* Other registers are not supported yet */
            warn_report("[Digital Signature] Unsupported read to %08lx\n", addr);
#endif
            break;
    }

#if DS_DEBUG
    info_report("[Digital Signature] Reading from %08lx (%08lx)\n", addr, r);
#endif

    return r;
}

static void esp_ds_write(void *opaque, hwaddr addr, uint64_t value, unsigned int size)
{
    ESPDsState *s = ESP_DS(opaque);

    /* Only support word aligned access for the moment */
    if (size != sizeof(uint32_t)) {
        error_report("[Digital Signature] Only 32-bit word access supported at the moment");
    }

    switch (addr) {
        case A_DS_MEM_Y_BLOCK_BASE ... (A_DS_MEM_Y_BLOCK_BASE + ESP_DS_MAX_MEM_BLK_SIZE - 1):
            s->y_mem[(addr - A_DS_MEM_Y_BLOCK_BASE) / sizeof(uint32_t)] = (uint32_t) value;
            break;

        case A_DS_MEM_M_BLOCK_BASE ... (A_DS_MEM_M_BLOCK_BASE + ESP_DS_MAX_MEM_BLK_SIZE - 1):
            s->m_mem[(addr - A_DS_MEM_M_BLOCK_BASE) / sizeof(uint32_t)] = (uint32_t) value;
            break;

        case A_DS_MEM_RB_BLOCK_BASE ... (A_DS_MEM_RB_BLOCK_BASE + ESP_DS_MAX_MEM_BLK_SIZE - 1):
            s->rb_mem[(addr - A_DS_MEM_RB_BLOCK_BASE) / sizeof(uint32_t)] = (uint32_t) value;
            break;

        case A_DS_MEM_BOX_BLOCK_BASE ... (A_DS_MEM_BOX_BLOCK_BASE + ESP_DS_BOX_MEM_BLK_SIZE - 1):
            s->box_mem[(addr - A_DS_MEM_BOX_BLOCK_BASE) / sizeof(uint32_t)] = (uint32_t) value;
            break;

        case A_DS_MEM_X_BLOCK_BASE ... (A_DS_MEM_X_BLOCK_BASE + ESP_DS_MAX_MEM_BLK_SIZE - 1):
            s->x_mem[(addr - A_DS_MEM_X_BLOCK_BASE) / sizeof(uint32_t)] = (uint32_t) value;
            break;

        case A_DS_IV_0_REG...A_DS_IV_3_REG:
            s->iv[(addr - A_DS_IV_0_REG) / sizeof(uint32_t)] = (uint32_t) value;
            break;

        case A_DS_SET_START_REG:
            esp_ds_generate_ds_key(s);
            break;

        case A_DS_SET_ME_REG:
            esp_ds_calculate(s);
            break;

        case A_DS_SET_FINISH_REG:
            esp_ds_clear_buffers(s);
            break;

        case A_DS_DATE_REG:
        default:
#if DS_WARNING
            /* Other registers are not supported yet */
            warn_report("[Digital Signature] Unsupported write to %08lx (%08lx)\n", addr, value);
#endif
            break;
    }

#if DS_DEBUG
    info_report("[Digital Signature] Writing to %08lx (%08lx)\n", addr, value);
#endif
}

static const MemoryRegionOps esp_ds_ops = {
    .read =  esp_ds_read,
    .write = esp_ds_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp_ds_reset_hold(Object *obj, ResetType type)
{
    ESPDsState *s = ESP_DS(obj);
    esp_ds_clear_buffers(s);
    s->ds_signature_check = DS_SIGNATURE_PADDING_AND_MD_FAIL;
}

static void esp_ds_realize(DeviceState *dev, Error **errp)
{
    ESPDsState *s = ESP_DS(dev);

    /* Make sure HMAC was set or issue an error */
    if (s->hmac == NULL) {
        error_report("[Digital Signature] HMAC controller must be set!");
    }

    /* Make sure AES was set or issue an error */
    if (s->aes == NULL) {
        error_report("[Digital Signature] AES controller must be set!");
    }

    /* Make sure RSA was set or issue an error */
    if (s->rsa == NULL) {
        error_report("[Digital Signature] RSA controller must be set!");
    }

    /* Make sure SHA was set or issue an error */
    if (s->sha == NULL) {
        error_report("[Digital Signature] SHA controller must be set!");
    }

    ESPDsClass *class = ESP_DS_GET_CLASS(s);

    // Allocate memory blocks based on configured size
    s->y_mem = g_malloc0(class->mem_blk_size);
    s->m_mem = g_malloc0(class->mem_blk_size);
    s->rb_mem = g_malloc0(class->mem_blk_size);
    s->box_mem = g_malloc0(ESP_DS_BOX_MEM_BLK_SIZE);
    s->x_mem = g_malloc0(class->mem_blk_size);
    s->z_mem = g_malloc0(class->mem_blk_size);

    if (!s->y_mem || !s->m_mem || !s->rb_mem || !s->box_mem || !s->x_mem || !s->z_mem) {
        error_report("[Digital Signature] No more memory in host!");
        return;
    }
}

static void esp_ds_finalize(Object *obj)
{
    ESPDsState *s = ESP_DS(obj);
    g_free(s->y_mem);
    g_free(s->m_mem);
    g_free(s->rb_mem);
    g_free(s->box_mem);
    g_free(s->x_mem);
    g_free(s->z_mem);
}

static void esp_ds_init(Object *obj)
{
    ESPDsState *s = ESP_DS(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp_ds_ops, s,
                          TYPE_ESP_DS, ESP_DS_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void esp_ds_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = esp_ds_realize;
    rc->phases.hold = esp_ds_reset_hold;
}

static const TypeInfo esp_ds_info = {
    .name = TYPE_ESP_DS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESPDsState),
    .instance_init = esp_ds_init,
    .instance_finalize = esp_ds_finalize,
    .class_init = esp_ds_class_init,
    .class_size = sizeof(ESPDsClass),
    .abstract = true,
};

static void esp_ds_register_types(void)
{
    type_register_static(&esp_ds_info);
}

type_init(esp_ds_register_types)
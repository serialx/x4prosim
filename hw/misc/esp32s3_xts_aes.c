/*
 * ESP32-S3 XTS-AES emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "qemu/error-report.h"
#include "crypto/aes.h"
#include "crypto/xts.h"
#include "hw/misc/esp32s3_xts_aes.h"

#define XTS_AES_WARNING 0
#define XTS_AES_DEBUG   0

#define EFUSE_KEY_PURPOSE_XTS_AES_256_KEY_1     2
#define EFUSE_KEY_PURPOSE_XTS_AES_256_KEY_2     3
#define EFUSE_KEY_PURPOSE_XTS_AES_128_KEY       4

#define XTS_AES_KEY_SIZE_128                    32
#define XTS_AES_KEY_SIZE_256                    64
#define ESP32S3_XTS_AES_DATA_UNIT_SIZE          128
#define ESP32S3_XTS_AES_TWEAK_VALUE             0x3FFFFF80

struct xts_aes_keys_ctx {
    AES_KEY enc;
    AES_KEY dec;
};

static void xts_aes_encrypt(const void *ctx,
                            size_t length,
                            uint8_t *dst,
                            const uint8_t *src)
{
    const struct xts_aes_keys_ctx *aesctx = ctx;

    AES_encrypt(src, dst, &aesctx->enc);
}

static void xts_aes_decrypt(const void *ctx,
                            size_t length,
                            uint8_t *dst,
                            const uint8_t *src)
{
    const struct xts_aes_keys_ctx *aesctx = ctx;

    AES_decrypt(src, dst, &aesctx->dec);
}

static bool esp32s3_xts_aes_is_ciphertext_spi_visible(ESP32S3XtsAesState *s)
{
    return (s->state == XTS_AES_RELEASE);
}

static bool esp32s3_xts_aes_is_manual_enc_enabled(ESP32S3XtsAesState *s)
{
    ESP32S3ClockClass *clock_class = ESP32S3_CLOCK_GET_CLASS(s->clock);
    ESPEfuseClass *efuse_class = ESP_EFUSE_GET_CLASS(s->efuse);
    uint32_t ext_dev_enc_dec_ctrl_reg = clock_class->get_ext_dev_enc_dec_ctrl(s->clock);
    return ((ext_dev_enc_dec_ctrl_reg & 1) || ((ext_dev_enc_dec_ctrl_reg & 8) && (efuse_class->get_dis_download_man_encrypt == 0)));
}

static bool esp32s3_xts_aes_is_flash_enc_enabled(ESP32S3XtsAesState *s)
{
    ESPEfuseClass *efuse_class = ESP_EFUSE_GET_CLASS(s->efuse);
    uint32_t spi_boot_crypt_cnt = efuse_class->get_spi_boot_crypt_cnt(s->efuse);
    return (ctpop32(spi_boot_crypt_cnt) & 1);
}

static uint32_t esp32s3_xts_aes_get_linesize(ESP32S3XtsAesState *s)
{
    if (s->linesize == 0) {
        return 16;
    } else if (s->linesize == 1) {
        return 32;
    } else if (s->linesize == 2) {
        return 64;
    } else {
        error_report("[XTS-AES] Incorrect value of linesize");
        return 0;
    }
}

static uint32_t esp32s3_xts_aes_get_key_size(ESP32S3XtsAesState *s)
{
    ESPEfuseClass *efuse_class = ESP_EFUSE_GET_CLASS(s->efuse);

    for (int i = EFUSE_BLOCK_KEY0; i < EFUSE_BLOCK_KEY6; i++) {
        if (efuse_class->get_key_purpose(s->efuse, i) == EFUSE_KEY_PURPOSE_XTS_AES_256_KEY_1
         || efuse_class->get_key_purpose(s->efuse, i) == EFUSE_KEY_PURPOSE_XTS_AES_256_KEY_2) {
            return XTS_AES_KEY_SIZE_256;
        }
    }
    // A key of 128 bits seem to be present
    return XTS_AES_KEY_SIZE_128;
}

static void reverse_xts_aes_key(uint8_t *key)
{
    uint8_t temp;
    for (int j = 0; j < XTS_AES_KEY_SIZE_128 / 2; j++) {
        temp = key[j];
        key[j] = key[XTS_AES_KEY_SIZE_128 - j - 1];
        key[XTS_AES_KEY_SIZE_128 - j - 1] = temp;
    }
}

static void esp32s3_xts_aes_get_key(ESP32S3XtsAesState *s, uint8_t *key, uint32_t key_size)
{
    ESPEfuseClass *efuse_class = ESP_EFUSE_GET_CLASS(s->efuse);
    memset(key, 0, key_size);

    if (key_size == XTS_AES_KEY_SIZE_128) {
        for (int i = EFUSE_BLOCK_KEY0; i < EFUSE_BLOCK_KEY6; i++) {
            if (efuse_class->get_key_purpose(s->efuse, i) == EFUSE_KEY_PURPOSE_XTS_AES_128_KEY) {
                efuse_class->get_key(s->efuse, i, key);
                // flash encryption key is stored in reverse byte order in the efuse block, correct it
                reverse_xts_aes_key(key);
                return;
            }
        }
    } else {
        // key_size == XTS_AES_KEY_SIZE_256
        for (int i = EFUSE_BLOCK_KEY0; i < EFUSE_BLOCK_KEY6; i++) {
            if (efuse_class->get_key_purpose(s->efuse, i) == EFUSE_KEY_PURPOSE_XTS_AES_256_KEY_1) {
                efuse_class->get_key(s->efuse, i, key);
                reverse_xts_aes_key(key);
            } else if (efuse_class->get_key_purpose(s->efuse, i) == EFUSE_KEY_PURPOSE_XTS_AES_256_KEY_2) {
                efuse_class->get_key(s->efuse, i, key + XTS_AES_KEY_SIZE_128);
                reverse_xts_aes_key(key + XTS_AES_KEY_SIZE_128);
            }
        }
    }
}

static void esp32s3_xts_aes_read_ciphertext(ESP32S3XtsAesState *s, uint32_t* spi_data_regs, uint32_t* spi_data_size, uint32_t* spi_addr, uint32_t* spi_addr_size)
{
    *spi_data_size = esp32s3_xts_aes_get_linesize(s);
    memcpy(spi_data_regs, s->ciphertext, *spi_data_size);
    /* Right shift address by 8 as the target memory space is a 24-bit address */
    *spi_addr = (cpu_to_be32(s->physical_addr)) >> 8;
    *spi_addr_size = 24 / 8;
}

static void esp32s3_xts_aes_encrypt(ESP32S3XtsAesState *s)
{
    uint8_t tweak[16];
    uint32_t linesize = esp32s3_xts_aes_get_linesize(s);

    uint8_t input_plaintext[ESP32S3_XTS_AES_DATA_UNIT_SIZE];
    uint8_t input_plaintext_reversed[ESP32S3_XTS_AES_DATA_UNIT_SIZE];
    uint8_t output_ciphertext[ESP32S3_XTS_AES_DATA_UNIT_SIZE];
    uint8_t output_ciphertext_reversed[ESP32S3_XTS_AES_DATA_UNIT_SIZE];

    *((uint32_t *) tweak) = cpu_to_le32((s->physical_addr & ESP32S3_XTS_AES_TWEAK_VALUE) + (s->destination << 30));
    memset(tweak + 4, 0, 12);

    uint32_t efuse_key_size = esp32s3_xts_aes_get_key_size(s);

    uint8_t *efuse_key = g_malloc(efuse_key_size * sizeof(uint8_t));

    if (efuse_key == NULL) {
        error_report("[XTS-AES] Memory allocation for the efuse key failed");
        return;
    }

    esp32s3_xts_aes_get_key(s, efuse_key, efuse_key_size);

    struct xts_aes_keys_ctx aesdata = {};
    struct xts_aes_keys_ctx aestweak = {};

    AES_set_encrypt_key(efuse_key, efuse_key_size / 2 * 8, &aesdata.enc);
    AES_set_decrypt_key(efuse_key, efuse_key_size / 2 * 8, &aesdata.dec);
    AES_set_encrypt_key(efuse_key + efuse_key_size / 2, efuse_key_size / 2 * 8, &aestweak.enc);
    AES_set_decrypt_key(efuse_key + efuse_key_size / 2, efuse_key_size / 2 * 8, &aestweak.dec);

    memset(input_plaintext, 0, ESP32S3_XTS_AES_DATA_UNIT_SIZE);

    uint32_t plaintext_offs = (s->physical_addr % (ESP32S3_XTS_AES_PLAIN_REG_CNT * 4));
    uint32_t pad_left = s->physical_addr % ESP32S3_XTS_AES_DATA_UNIT_SIZE;
    memcpy(input_plaintext + pad_left, ((uint8_t*)s->plaintext) + plaintext_offs, linesize);

    for (int i = 0; i < ESP32S3_XTS_AES_DATA_UNIT_SIZE; i++) {
        input_plaintext_reversed[i] = input_plaintext[ESP32S3_XTS_AES_DATA_UNIT_SIZE-i-1];
    }

    xts_encrypt(&aesdata, &aestweak,
                xts_aes_encrypt,
                xts_aes_decrypt,
                tweak, ESP32S3_XTS_AES_DATA_UNIT_SIZE, output_ciphertext_reversed, input_plaintext_reversed);

    for (int i = 0; i < ESP32S3_XTS_AES_DATA_UNIT_SIZE; i++) {
        output_ciphertext[i] = output_ciphertext_reversed[ESP32S3_XTS_AES_DATA_UNIT_SIZE-i-1];
    }

    memset(s->ciphertext, 0, ESP32S3_XTS_AES_PLAIN_REG_CNT * sizeof(uint32_t));
    memcpy(s->ciphertext, output_ciphertext + pad_left, linesize);
    g_free(efuse_key);
}

static void esp32s3_xts_aes_decrypt(ESP32S3XtsAesState *s, uint32_t physical_address, uint8_t *data, uint32_t size)
{
    uint8_t tweak[16];
    struct xts_aes_keys_ctx aesdata = {};
    struct xts_aes_keys_ctx aestweak = {};

    uint32_t efuse_key_size = esp32s3_xts_aes_get_key_size(s);

    uint8_t *efuse_key = g_malloc(efuse_key_size * sizeof(uint8_t));

    if (efuse_key == NULL) {
        error_report("[XTS-AES] Memory allocation for the efuse key failed");
        return;
    }

    esp32s3_xts_aes_get_key(s, efuse_key, efuse_key_size);

    AES_set_encrypt_key(efuse_key, efuse_key_size / 2 * 8, &aesdata.enc);
    AES_set_decrypt_key(efuse_key, efuse_key_size / 2 * 8, &aesdata.dec);
    AES_set_encrypt_key(efuse_key + efuse_key_size / 2, efuse_key_size / 2 * 8, &aestweak.enc);
    AES_set_decrypt_key(efuse_key + efuse_key_size / 2, efuse_key_size / 2 * 8, &aestweak.dec);

    uint8_t input_ciphertext[ESP32S3_XTS_AES_DATA_UNIT_SIZE];
    uint8_t input_ciphertext_reversed[ESP32S3_XTS_AES_DATA_UNIT_SIZE];
    uint8_t output_plaintext[ESP32S3_XTS_AES_DATA_UNIT_SIZE];
    uint8_t output_plaintext_reversed[ESP32S3_XTS_AES_DATA_UNIT_SIZE];

    for (int i = 0; i < size; i += ESP32S3_XTS_AES_DATA_UNIT_SIZE) {
        *((uint32_t *) tweak) = cpu_to_le32(((physical_address + i) & ESP32S3_XTS_AES_TWEAK_VALUE) + (s->destination << 30));
        memset(tweak + 4, 0, 12);

        memcpy(input_ciphertext, data + i, ESP32S3_XTS_AES_DATA_UNIT_SIZE);
        for (int j = 0; j < ESP32S3_XTS_AES_DATA_UNIT_SIZE; j++) {
            input_ciphertext_reversed[j] = input_ciphertext[ESP32S3_XTS_AES_DATA_UNIT_SIZE-j-1];
        }

        xts_decrypt(&aesdata, &aestweak,
                    xts_aes_encrypt,
                    xts_aes_decrypt,
                    tweak, ESP32S3_XTS_AES_DATA_UNIT_SIZE, output_plaintext_reversed, input_ciphertext_reversed);

        for (int j = 0; j < ESP32S3_XTS_AES_DATA_UNIT_SIZE; j++) {
            output_plaintext[j] = output_plaintext_reversed[ESP32S3_XTS_AES_DATA_UNIT_SIZE-j-1];
        }

        memcpy(data + i, output_plaintext, ESP32S3_XTS_AES_DATA_UNIT_SIZE);
    }
    g_free(efuse_key);
}

static uint64_t esp32s3_xts_aes_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32S3XtsAesState *s = ESP32S3_XTS_AES(opaque);

    uint64_t r = 0;
    switch (addr) {
        case A_XTS_AES_DATE_REG:
            r = 0x20200111;
            break;

        case A_XTS_AES_PLAIN_0_REG ... A_XTS_AES_PLAIN_15_REG:
            r = s->plaintext[(addr - A_XTS_AES_PLAIN_0_REG) / sizeof(uint32_t)];
            break;

        case A_XTS_AES_LINESIZE_REG:
            r = s->linesize;
            break;

        case A_XTS_AES_DESTINATION_REG:
            r = s->destination;
            break;

        case A_XTS_AES_PHYSICAL_ADDRESS_REG:
            r = s->physical_addr;
            break;

        case A_XTS_AES_STATE_REG:
            r = s->state;
            break;

        default:
#if XTS_AES_WARNING
            /* Other registers are not supported yet */
            warn_report("[XTS_AES] Unsupported read to %08lx\n", addr);
#endif
            break;
    }

#if XTS_AES_DEBUG
    info_report("[XTS_AES] Reading from %08lx (%08lx)\n", addr, r);
#endif

    return r;
}


static void esp32s3_xts_aes_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    ESP32S3XtsAesState *s = ESP32S3_XTS_AES(opaque);

    switch (addr) {
        case A_XTS_AES_LINESIZE_REG:
            s->linesize = FIELD_EX32(value, XTS_AES_LINESIZE_REG, XTS_AES_LINESIZE);
            break;

        case A_XTS_AES_DESTINATION_REG:
            s->destination = FIELD_EX32(value, XTS_AES_DESTINATION_REG, XTS_AES_DESTINATION);
            break;

        case A_XTS_AES_PHYSICAL_ADDRESS_REG:
            s->physical_addr = FIELD_EX32(value, XTS_AES_PHYSICAL_ADDRESS_REG, XTS_AES_PHYSICAL_ADDRESS);
            if (s->physical_addr > 0x00FFFFFF) {
                error_report("[XTS-AES] Physical Adress greater than 0x00FFFFFF");
            }
            break;

        case A_XTS_AES_PLAIN_0_REG ... A_XTS_AES_PLAIN_15_REG:
            s->plaintext[(addr - A_XTS_AES_PLAIN_0_REG) / sizeof(uint32_t)] = value;
            break;

        case A_XTS_AES_TRIGGER_REG:
            if (FIELD_EX32(value, XTS_AES_TRIGGER_REG, XTS_AES_TRIGGER) == 1) {
                s->state = XTS_AES_BUSY;
                esp32s3_xts_aes_encrypt(s);
                s->state= XTS_AES_DONE;
            }
            break;

        case A_XTS_AES_RELEASE_REG:
            if (FIELD_EX32(value, XTS_AES_RELEASE_REG, XTS_AES_RELEASE) == 1) {
                // "Grant SPI1 access for the ciphertext"
                s->state = XTS_AES_RELEASE;
            }
            break;

        case A_XTS_AES_DESTROY_REG:
            FIELD_EX32(value, XTS_AES_DESTROY_REG, XTS_AES_DESTROY);
            s->state = XTS_AES_IDLE;
            memset(s->ciphertext, 0, ESP32S3_XTS_AES_PLAIN_REG_CNT * sizeof(uint32_t));
            break;

        default:
#if XTS_AES_WARNING
            /* Other registers are not supported yet */
            warn_report("[XTS_AES] Unsupported write to %08lx (%08lx)\n", addr, value);
#endif
            break;
    }

#if XTS_AES_DEBUG
    info_report("[XTS_AES] Writing to %08lx (%08lx)\n", addr, value);
#endif

}

static const MemoryRegionOps esp32s3_xts_aes_ops = {
    .read =  esp32s3_xts_aes_read,
    .write = esp32s3_xts_aes_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32s3_xts_aes_reset_hold(Object *obj, ResetType type)
{
    ESP32S3XtsAesState *s = ESP32S3_XTS_AES(obj);
    memset(s->plaintext, 0, ESP32S3_XTS_AES_PLAIN_REG_CNT * sizeof(uint32_t));
    memset(s->ciphertext, 0, ESP32S3_XTS_AES_PLAIN_REG_CNT * sizeof(uint32_t));
    s->state = XTS_AES_IDLE;
    s->linesize = 0;
    s->destination = 0;
    s->physical_addr = 0;
}

static void esp32s3_xts_aes_realize(DeviceState *dev, Error **errp)
{
    ESP32S3XtsAesState *s = ESP32S3_XTS_AES(dev);

    /* Make sure Efuse was set of issue an error */
    if (s->efuse == NULL) {
        error_report("[XTS_AES] Efuse controller must be set!");
    }

    /* Make sure Clock was set or issue an error */
    if (s->clock == NULL) {
        error_report("[XTS_AES] Clock controller must be set!");
    }
}

static void esp32s3_xts_aes_init(Object *obj)
{
    ESP32S3XtsAesState *s = ESP32S3_XTS_AES(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32s3_xts_aes_ops, s,
                          TYPE_ESP32S3_XTS_AES, ESP32S3_XTS_AES_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void esp32s3_xts_aes_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ESP32S3XtsAesClass* esp32s3_xts_aes = ESP32S3_XTS_AES_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = esp32s3_xts_aes_realize;
    rc->phases.hold = esp32s3_xts_aes_reset_hold;

    esp32s3_xts_aes->is_ciphertext_spi_visible = esp32s3_xts_aes_is_ciphertext_spi_visible;
    esp32s3_xts_aes->is_flash_enc_enabled = esp32s3_xts_aes_is_flash_enc_enabled;
    esp32s3_xts_aes->is_manual_enc_enabled = esp32s3_xts_aes_is_manual_enc_enabled;
    esp32s3_xts_aes->decrypt = esp32s3_xts_aes_decrypt;
    esp32s3_xts_aes->read_ciphertext = esp32s3_xts_aes_read_ciphertext;
}

static const TypeInfo esp32s3_xts_aes_info = {
    .name = TYPE_ESP32S3_XTS_AES,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32S3XtsAesState),
    .instance_init = esp32s3_xts_aes_init,
    .class_init = esp32s3_xts_aes_class_init,
    .class_size = sizeof(ESP32S3XtsAesClass)
};

static void esp32s3_xts_aes_register_types(void)
{
    type_register_static(&esp32s3_xts_aes_info);
}

type_init(esp32s3_xts_aes_register_types)

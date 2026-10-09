/*
 * eFuse emulation for recent ESP32-series chip (ESP32-S3 and newer)
 *
 * Copyright (c) 2024 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include "qemu/timer.h"
#include "hw/core/registerfields.h"
#include "hw/core/sysbus.h"
#include "system/block-backend.h"
#include "qemu/error-report.h"

#define TYPE_ESP_EFUSE "nvram.esp.efuse"
#define ESP_EFUSE(obj) OBJECT_CHECK(ESPEfuseState, (obj), TYPE_ESP_EFUSE)
#define ESP_EFUSE_GET_CLASS(obj) OBJECT_GET_CLASS(ESPEfuseClass, obj, TYPE_ESP_EFUSE)
#define ESP_EFUSE_CLASS(klass) OBJECT_CLASS_CHECK(ESPEfuseClass, klass, TYPE_ESP_EFUSE)

/**
 * The last register of the efuse I/O space is EFUSE_DBG_ERASE_ALL (which is 4 bytes big)
 */
#define ESP_EFUSE_IO_RANGE_SIZE (A_EFUSE_DBG_ERASE_ALL + 4)

/**
 * Size of the efuses in bytes on the ESP
 */
#define ESP_EFUSE_BYTE_COUNT        (4096/4)

/**
 * Number of data registers used when programming efuses
 */
#define ESP_EFUSE_PGM_DATA_COUNT    8

/**
 * Number of check registers used when programming efuses
 */
#define ESP_EFUSE_PGM_CHECK_COUNT   3

/**
 * Size of BLOCK0 and BLOCK1 in 32-bit word unit
 */
#define ESP_EFUSE_BLOCK0_WORDS      6
#define ESP_EFUSE_BLOCK1_WORDS      6

/**
 * Enumeration of the blocks that can be retrieved by other module in the system being emulated
 */
typedef enum {
    EFUSE_BLOCK0 = 0,
    EFUSE_MAC_SPI_SYS_0 = 1,
    EFUSE_BLOCK_SYS_DATA = 2,
    EFUSE_BLOCK_USR_DATA = 3,
    EFUSE_BLOCK_KEY0 = 4,
    EFUSE_BLOCK_KEY1 = 5,
    EFUSE_BLOCK_KEY2 = 6,
    EFUSE_BLOCK_KEY3 = 7,
    EFUSE_BLOCK_KEY4 = 8,
    EFUSE_BLOCK_KEY5 = 9,
    EFUSE_BLOCK_KEY6 = 10,
    EFUSE_BLOCK_MAX,
} EfuseBlocksIdx;

/**
 * @brief Definition of the efuse blocks, the goal is to keep them the most standard possible. As such, details such
 * as the BLOCK0 inner fields have been omitted.
 */
struct ESPEfuseBlocks {
    /* BLOCK 0 */
    uint32_t rd_wr_dis;
    uint32_t rd_repeat_data0;
    uint32_t rd_repeat_data1;
    uint32_t rd_repeat_data2;
    uint32_t rd_repeat_data3;
    uint32_t rd_repeat_data4;
    /* BLOCK 1 */
    uint32_t rd_mac_spi_sys_0;
    uint32_t rd_mac_spi_sys_1;
    uint32_t rd_mac_spi_sys_2;
    uint32_t rd_mac_spi_sys_3;
    uint32_t rd_mac_spi_sys_4;
    uint32_t rd_mac_spi_sys_5;
    /* BLOCK 2 */
    uint32_t rd_sys_part1_data0;
    uint32_t rd_sys_part1_data1;
    uint32_t rd_sys_part1_data2;
    uint32_t rd_sys_part1_data3;
    uint32_t rd_sys_part1_data4;
    uint32_t rd_sys_part1_data5;
    uint32_t rd_sys_part1_data6;
    uint32_t rd_sys_part1_data7;
    /* BLOCK 3 */
    uint32_t rd_usr_data0;
    uint32_t rd_usr_data1;
    uint32_t rd_usr_data2;
    uint32_t rd_usr_data3;
    uint32_t rd_usr_data4;
    uint32_t rd_usr_data5;
    uint32_t rd_usr_data6;
    uint32_t rd_usr_data7;
    /* BLOCK 4 */
    uint32_t rd_key0_data0;
    uint32_t rd_key0_data1;
    uint32_t rd_key0_data2;
    uint32_t rd_key0_data3;
    uint32_t rd_key0_data4;
    uint32_t rd_key0_data5;
    uint32_t rd_key0_data6;
    uint32_t rd_key0_data7;
    /* BLOCK 5 */
    uint32_t rd_key1_data0;
    uint32_t rd_key1_data1;
    uint32_t rd_key1_data2;
    uint32_t rd_key1_data3;
    uint32_t rd_key1_data4;
    uint32_t rd_key1_data5;
    uint32_t rd_key1_data6;
    uint32_t rd_key1_data7;
    /* BLOCK 6 */
    uint32_t rd_key2_data0;
    uint32_t rd_key2_data1;
    uint32_t rd_key2_data2;
    uint32_t rd_key2_data3;
    uint32_t rd_key2_data4;
    uint32_t rd_key2_data5;
    uint32_t rd_key2_data6;
    uint32_t rd_key2_data7;
    /* BLOCK 7 */
    uint32_t rd_key3_data0;
    uint32_t rd_key3_data1;
    uint32_t rd_key3_data2;
    uint32_t rd_key3_data3;
    uint32_t rd_key3_data4;
    uint32_t rd_key3_data5;
    uint32_t rd_key3_data6;
    uint32_t rd_key3_data7;
    /* BLOCK 8 */
    uint32_t rd_key4_data0;
    uint32_t rd_key4_data1;
    uint32_t rd_key4_data2;
    uint32_t rd_key4_data3;
    uint32_t rd_key4_data4;
    uint32_t rd_key4_data5;
    uint32_t rd_key4_data6;
    uint32_t rd_key4_data7;
    /* BLOCK 9 */
    uint32_t rd_key5_data0;
    uint32_t rd_key5_data1;
    uint32_t rd_key5_data2;
    uint32_t rd_key5_data3;
    uint32_t rd_key5_data4;
    uint32_t rd_key5_data5;
    uint32_t rd_key5_data6;
    uint32_t rd_key5_data7;
    /* BLOCK 10 */
    uint32_t rd_sys_part2_data0;
    uint32_t rd_sys_part2_data1;
    uint32_t rd_sys_part2_data2;
    uint32_t rd_sys_part2_data3;
    uint32_t rd_sys_part2_data4;
    uint32_t rd_sys_part2_data5;
    uint32_t rd_sys_part2_data6;
    uint32_t rd_sys_part2_data7;
};

typedef struct ESPEfuseBlocks ESPEfuseBlocks;


/**
 * @brief Structure definition of the efuse I/O registers. This should be common to all the recent ESP devices (S3 and later)
 */
struct ESPEfuseRegs {
    uint32_t pgm_data[ESP_EFUSE_PGM_DATA_COUNT];             /*Registers that stores data to be programmed.*/
    uint32_t pgm_check[ESP_EFUSE_PGM_CHECK_COUNT];           /*Registers that stores the RS code to be programmed.*/
    ESPEfuseBlocks blocks;

    /* I/O registers */
    uint32_t clk;
    uint32_t conf;
    uint32_t status;
    uint32_t cmd;
    uint32_t int_raw;
    uint32_t int_st;
    uint32_t int_ena;
    uint32_t int_clr;
    uint32_t dac_conf;
    uint32_t rd_tim_conf;
    uint32_t wr_tim_conf1;
    uint32_t wr_tim_conf2;
    uint32_t dbg_erase_all;
};

typedef struct ESPEfuseRegs ESPEfuseRegs;

typedef struct ESPEfuseState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    /* In case no block was given by the user, use the mirror as a file in RAM */
    BlockBackend *blk;
    void* mirror;

    qemu_irq irq;
    /* Use a mirror to make sure the operation value did not change between the moment
     * it is scheduled and the moment it actually happens. */
    uint32_t op_cmd_mirror;
    QEMUTimer op_timer;

    ESPEfuseRegs efuses;
    /* Same efuses as the ones above, but protected blocks are not cleared.
     * This will be used by C3 emulated encryption modules */
    ESPEfuseRegs efuses_internal;
} ESPEfuseState;

typedef struct ESPEfuseClass {
    SysBusDeviceClass parent_class;
    /* Virtual methods */
    uint32_t (*get_spi_boot_crypt_cnt)(ESPEfuseState *s);
    uint32_t (*get_dis_download_man_encrypt)(ESPEfuseState *s);
    bool     (*get_key)(ESPEfuseState *s, EfuseBlocksIdx efuse_block_num, uint8_t* efuse_key);
    uint32_t (*get_key_purpose)(ESPEfuseState *s, EfuseBlocksIdx efuse_block_num);
} ESPEfuseClass;


/* Fields for the blocks, only define the necessary ones */
REG32(EFUSE_PGM_DATA0_REG, 0x00)

REG32(EFUSE_PGM_CHECK_VALUE2_REG, 0x28)

REG32(EFUSE_RD_WR_DIS_REG, 0x2C)

REG32(EFUSE_RD_REPEAT_DATA0, 0x30)
    /* These fields are common to both the C3 and S3 */
    FIELD(EFUSE_RD_REPEAT_DATA0, DIS_DOWNLOAD_MANUAL_ENCRYPT, 20, 1)
    FIELD(EFUSE_RD_REPEAT_DATA0, RD_DIS, 0, 7)

REG32(EFUSE_RD_REPEAT_DATA1, 0x34)
    FIELD(EFUSE_RD_REPEAT_DATA1, KEY_PURP_1, 28, 4)
    FIELD(EFUSE_RD_REPEAT_DATA1, KEY_PURP_0, 24, 4)
    FIELD(EFUSE_RD_REPEAT_DATA1, SPI_BOOT_CRYPT_CNT, 18, 3)

REG32(EFUSE_RD_REPEAT_DATA2, 0x38)
    FIELD(EFUSE_RD_REPEAT_DATA2, KEY_PURP_5, 12, 4)
    FIELD(EFUSE_RD_REPEAT_DATA2, KEY_PURP_4, 8, 4)
    FIELD(EFUSE_RD_REPEAT_DATA2, KEY_PURP_3, 4, 4)
    FIELD(EFUSE_RD_REPEAT_DATA2, KEY_PURP_2, 0, 4)

REG32(EFUSE_RD_SYS_PART2_DATA7_REG, 0x178)


/* Fields for the efuse registers that are not blocks (conf, command, etc...)
 * This gives more flexibility than using a structure offset, which may change and would result in
 * bugs that are hard to track. */
REG32(EFUSE_CLK, 0x01C8)

REG32(EFUSE_CONF, 0x01CC)
    FIELD(EFUSE_CONF, OP_CODE, 0, 16)

REG32(EFUSE_STATUS, 0x01D0)
    FIELD(EFUSE_STATUS, STATE, 0, 4)

REG32(EFUSE_CMD, 0x01D4)
    FIELD(EFUSE_CMD, BLK_NUM, 2, 4)
    FIELD(EFUSE_CMD, PGM, 1, 1)
    FIELD(EFUSE_CMD, READ, 0, 1)


REG32(EFUSE_INT_RAW, 0x01D8)
    FIELD(EFUSE_INT_RAW, PGM_DONE, 1, 1)
    FIELD(EFUSE_INT_RAW, READ_DONE, 0, 1)

REG32(EFUSE_INT_ST, 0x01DC)

REG32(EFUSE_INT_ENA, 0x01E0)

REG32(EFUSE_INT_CLR, 0x01E4)

REG32(EFUSE_DAC_CONF, 0x01E8)

REG32(EFUSE_RD_TIM_CONF, 0x01EC)

REG32(EFUSE_WR_TIM_CONF1, 0x01F4)

REG32(EFUSE_WR_TIM_CONF2, 0x01F8)

REG32(EFUSE_DBG_ERASE_ALL, 0x0200)

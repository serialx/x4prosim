/*
 * Copyright (c) 2019 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "gdbstub/helpers.h"
#include "qemu/host-utils.h"

#include "core-esp32s3/core-isa.h"
#include "overlay_tool.h"

#define xtensa_modules xtensa_modules_esp32s3
#include "core-esp32s3/xtensa-modules.inc.c"

extern const XtensaOpcodeTranslators xtensa_tie_opcodes;

static const XtensaOpcodeTranslators * esp32s3_opcode_translators[] = {
    &xtensa_core_opcodes,
    &xtensa_fpu_opcodes,
    &xtensa_tie_opcodes,
    NULL
};

static XtensaConfig xtensa_core_esp32s3 __attribute__((unused)) = {
    .name = "esp32s3",
    .gdb_regmap = {
        .reg = {
#include "core-esp32s3/gdb-config.inc.c"
        }
    },
    .isa_internal = &xtensa_modules,
    .clock_freq_khz = 40000,
    .opcode_translators = esp32s3_opcode_translators,
    DEFAULT_SECTIONS
};

REGISTER_CORE(xtensa_core_esp32s3)

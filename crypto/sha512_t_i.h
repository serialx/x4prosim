/*
 * SHA-512/t emulation for ESP32 series
 *
 * Copyright (c) 2024 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#ifndef SHA512_T_I_H
#define SHA512_T_I_H
#include "qemu/osdep.h"

#include "sha512_i.h"

void sha512_t_init_message(uint32_t *buffer, size_t buf_len, uint32_t t,
                          uint32_t t_len);
void sha512_t_init(struct sha512_state *md);

#endif /* SHA512_T_I_H */

/*
 * SHA-512/t emulation for ESP32 series
 *
 * Copyright (c) 2024 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "sha512_t_i.h"

#define CONST64(n) n ## ULL

void sha512_t_init_message(uint32_t *buffer, size_t buf_len, uint32_t t,
                          uint32_t t_len)
{
    /*
     * hex representation of "SHA-512/t"
     */
    buffer[0] = 0x2D414853UL;     /* SHA- */
    buffer[1] = 0x2F323135UL;     /* 512/ */
    buffer[2] = t;               /* t */

    for (int i = 3; i < buf_len; i++) {
        buffer[i] = 0;           /* padding */
    }

    buffer[buf_len - 1] = t_len;  /* length of message (SHA-512/t) */
}

/*
 * Initialize the hash state
 */
void sha512_t_init(struct sha512_state *md)
{
    sha512_init(md);

    for (int i = 0; i < 8; i++) {
        md->state[i] ^= CONST64(0xa5a5a5a5a5a5a5a5);
    }
}

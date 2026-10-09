/* LibTomCrypt, modular cryptographic library -- Tom St Denis */
/* SPDX-License-Identifier: Unlicense */

#include "qemu/osdep.h"
#include "sha512_224_i.h"
/*
 * This is based on SHA512-224 implementation in LibTomCrypt that was
 * released into public domain by Tom St Denis.
 */

#define CONST64(n) n ## ULL

void sha512_224_init(struct sha512_state *md)
{
    md->state[0] = CONST64(0x8C3D37C819544DA2);
    md->state[1] = CONST64(0x73E1996689DCD4D6);
    md->state[2] = CONST64(0x1DFAB7AE32FF9C82);
    md->state[3] = CONST64(0x679DD514582F9FCF);
    md->state[4] = CONST64(0x0F6D2B697BD44DA8);
    md->state[5] = CONST64(0x77E36F7304C48942);
    md->state[6] = CONST64(0x3F9D85A86A1D36C8);
    md->state[7] = CONST64(0x1112E6AD91D692A1);
}

/* ===== end - public domain SHA512-224 implementation ===== */

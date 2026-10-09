/* LibTomCrypt, modular cryptographic library -- Tom St Denis */
/* SPDX-License-Identifier: Unlicense */

#include "qemu/osdep.h"
#include "sha512_256_i.h"

/* ===== start - public domain SHA512-256 implementation ===== */

/*
 * This is based on SHA512-256 implementation in LibTomCrypt that was
 * released into public domain by Tom St Denis.
 */

#define CONST64(n) n ## ULL

void sha512_256_init(struct sha512_state *md)
{
    md->state[0] = CONST64(0x22312194FC2BF72C);
    md->state[1] = CONST64(0x9F555FA3C84C64C2);
    md->state[2] = CONST64(0x2393B86B6F53B151);
    md->state[3] = CONST64(0x963877195940EABD);
    md->state[4] = CONST64(0x96283EE2A88EFFE3);
    md->state[5] = CONST64(0xBE5E1E2553863992);
    md->state[6] = CONST64(0x2B0199FC2C85B8AA);
    md->state[7] = CONST64(0x0EB72DDC81C52CA2);
}

/* ===== end - public domain SHA512-256 implementation ===== */

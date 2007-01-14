/*
 * AES 128 bit decryption
 * Copyright (c) 2007 Reimar Doeffinger.
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Based on public domain AES reference code by Paulo Barreto, Vincent Rijmen
 */

#include "common.h"
#include "aes128.h"

#ifdef CONFIG_GCRYPT
AES128Context *aes128_init(void) {
    AES128Context *res = av_malloc(sizeof(*res));
    gcry_cipher_open(&res->ch, GCRY_CIPHER_AES, GCRY_CIPHER_MODE_CBC, 0);
    return res;
}
void aes128_set_key(AES128Context *c, const uint8_t *key) {
    gcry_cipher_ctl(c->ch, GCRYCTL_SET_KEY, key, 16);
}
void aes128_cbc_decrypt(AES128Context *c, uint8_t *mem, int blockcnt, uint8_t *IV) {
    blockcnt <<= 4;
    gcry_cipher_ctl(c->ch, GCRYCTL_SET_IV, IV, 16);
    memcpy(IV, &mem[blockcnt - 16], 16);
    gcry_cipher_decrypt(c->ch, mem, blockcnt, mem, blockcnt);
}
#else
static const uint8_t rcon[10] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};
static const uint8_t logtbl[256] = {
      0,   0,  25,   1,  50,   2,  26, 198,  75, 199,  27, 104,  51, 238, 223,   3,
    100,   4, 224,  14,  52, 141, 129, 239,  76, 113,   8, 200, 248, 105,  28, 193,
    125, 194,  29, 181, 249, 185,  39, 106,  77, 228, 166, 114, 154, 201,   9, 120,
    101,  47, 138,   5,  33,  15, 225,  36,  18, 240, 130,  69,  53, 147, 218, 142,
    150, 143, 219, 189,  54, 208, 206, 148,  19,  92, 210, 241,  64,  70, 131,  56,
    102, 221, 253,  48, 191,   6, 139,  98, 179,  37, 226, 152,  34, 136, 145,  16,
    126, 110,  72, 195, 163, 182,  30,  66,  58, 107,  40,  84, 250, 133,  61, 186,
     43, 121,  10,  21, 155, 159,  94, 202,  78, 212, 172, 229, 243, 115, 167,  87,
    175,  88, 168,  80, 244, 234, 214, 116,  79, 174, 233, 213, 231, 230, 173, 232,
     44, 215, 117, 122, 235,  22,  11, 245,  89, 203,  95, 176, 156, 169,  81, 160,
    127,  12, 246, 111,  23, 196,  73, 236, 216,  67,  31,  45, 164, 118, 123, 183,
    204, 187,  62,  90, 251,  96, 177, 134,  59,  82, 161, 108, 170,  85,  41, 157,
    151, 178, 135, 144,  97, 190, 220, 252, 188, 149, 207, 205,  55,  63,  91, 209,
     83,  57, 132,  60,  65, 162, 109,  71,  20,  42, 158,  93,  86, 242, 211, 171,
     68,  17, 146, 217,  35,  32,  46, 137, 180, 124, 184,  38, 119, 153, 227, 165,
    103,  74, 237, 222, 197,  49, 254,  24,  13,  99, 140, 128, 192, 247, 112,   7
};
static const uint8_t invsubst[256] = {
     82,   9, 106, 213,  48,  54, 165,  56, 191,  64, 163, 158, 129, 243, 215, 251,
    124, 227,  57, 130, 155,  47, 255, 135,  52, 142,  67,  68, 196, 222, 233, 203,
     84, 123, 148,  50, 166, 194,  35,  61, 238,  76, 149,  11,  66, 250, 195,  78,
      8,  46, 161, 102,  40, 217,  36, 178, 118,  91, 162,  73, 109, 139, 209,  37,
    114, 248, 246, 100, 134, 104, 152,  22, 212, 164,  92, 204,  93, 101, 182, 146,
    108, 112,  72,  80, 253, 237, 185, 218,  94,  21,  70,  87, 167, 141, 157, 132,
    144, 216, 171,   0, 140, 188, 211,  10, 247, 228,  88,   5, 184, 179,  69,   6,
    208,  44,  30, 143, 202,  63,  15,   2, 193, 175, 189,   3,   1,  19, 138, 107,
     58, 145,  17,  65,  79, 103, 220, 234, 151, 242, 207, 206, 240, 180, 230, 115,
    150, 172, 116,  34, 231, 173,  53, 133, 226, 249,  55, 232,  28, 117, 223, 110,
     71, 241,  26, 113,  29,  41, 197, 137, 111, 183,  98,  14, 170,  24, 190,  27,
    252,  86,  62,  75, 198, 210, 121,  32, 154, 219, 192, 254, 120, 205,  90, 244,
     31, 221, 168,  51, 136,   7, 199,  49, 177,  18,  16,  89,  39, 128, 236,  95,
     96,  81, 127, 169,  25, 181,  74,  13,  45, 229, 122, 159, 147, 201, 156, 239,
    160, 224,  59,  77, 174,  42, 245, 176, 200, 235, 187,  60, 131,  83, 153,  97,
     23,  43,   4, 126, 186, 119, 214,  38, 225, 105,  20,  99,  85,  33,  12, 125
};
#define XORBLOCK(a, rk) \
    ((uint64_t *)(a))[0] ^= ((uint64_t *)(rk))[0];\
    ((uint64_t *)(a))[1] ^= ((uint64_t *)(rk))[1];
#define COPYBLOCK(b, a) \
    ((uint64_t *)(b))[0] = ((uint64_t *)(a))[0];\
    ((uint64_t *)(b))[1] = ((uint64_t *)(a))[1];
#define SUBSTSHIFTROWS(b, a) \
    b[0]  = invsubst[a[0]];  b[1]  = invsubst[a[13]]; b[2]  = invsubst[a[10]];\
    b[3]  = invsubst[a[7]];  b[4]  = invsubst[a[4]];  b[5]  = invsubst[a[1]];\
    b[6]  = invsubst[a[14]]; b[7]  = invsubst[a[11]]; b[8]  = invsubst[a[8]];\
    b[9]  = invsubst[a[5]];  b[10] = invsubst[a[2]];  b[11] = invsubst[a[15]];\
    b[12] = invsubst[a[12]]; b[13] = invsubst[a[9]];  b[14] = invsubst[a[6]];\
    b[15] = invsubst[a[3]];
#define INVMIX(b, a) \
    ((uint32_t *)(b))[0] = c->multbl[0][a[0]]  ^ c->multbl[1][a[1]]  ^ c->multbl[2][a[2]]  ^ c->multbl[3][a[3]];\
    ((uint32_t *)(b))[1] = c->multbl[0][a[4]]  ^ c->multbl[1][a[5]]  ^ c->multbl[2][a[6]]  ^ c->multbl[3][a[7]];\
    ((uint32_t *)(b))[2] = c->multbl[0][a[8]]  ^ c->multbl[1][a[9]]  ^ c->multbl[2][a[10]] ^ c->multbl[3][a[11]];\
    ((uint32_t *)(b))[3] = c->multbl[0][a[12]] ^ c->multbl[1][a[13]] ^ c->multbl[2][a[14]] ^ c->multbl[3][a[15]];

#define MUL(a, b) ((a && b) ? invlogtbl[(logtbl[a] + logtbl[b])%255] : 0)
AES128Context *aes128_init(void) {
    AES128Context *c = av_mallocz(sizeof(*c));
    uint8_t *invlogtbl = av_malloc(256);
    uint8_t *tbl0, *tbl1, *tbl2, *tbl3;
    int i;
    for (i = 0; i < 256; i++) {
        c->subst[invsubst[i]] = i;
        invlogtbl[logtbl[i]] = i;
    }
    invlogtbl[255] = 1;
    tbl0 = (uint8_t *)c->multbl[0];
    tbl1 = (uint8_t *)c->multbl[1];
    tbl2 = (uint8_t *)c->multbl[2];;
    tbl3 = (uint8_t *)c->multbl[3];
    for (i = 0; i < 256; i++) {
        tbl0[4*i+0] = MUL(0xe, i); tbl0[4*i+1] = MUL(0x9, i);
        tbl0[4*i+2] = MUL(0xd, i); tbl0[4*i+3] = MUL(0xb, i);
        tbl1[4*i+0] = MUL(0xb, i); tbl1[4*i+1] = MUL(0xe, i);
        tbl1[4*i+2] = MUL(0x9, i); tbl1[4*i+3] = MUL(0xd, i);
        tbl2[4*i+0] = MUL(0xd, i); tbl2[4*i+1] = MUL(0xb, i);
        tbl2[4*i+2] = MUL(0xe, i); tbl2[4*i+3] = MUL(0x9, i);
        tbl3[4*i+0] = MUL(0x9, i); tbl3[4*i+1] = MUL(0xd, i);
        tbl3[4*i+2] = MUL(0xb, i); tbl3[4*i+3] = MUL(0xe, i);
    }
    av_free(invlogtbl);
    return c;
}

void aes128_set_key(AES128Context *c, const uint8_t *key) {
    uint8_t tmp[4][4];
    long r, i, j;
    memcpy(tmp, key, 16);
    memcpy(c->key[0], tmp, 16);
    for (r = 1; r < 11; r++) {
        for (i = 0; i < 4; i++) tmp[0][i] ^= c->subst[tmp[3][(i+1)&3]];
        tmp[0][0] ^= rcon[r - 1];
        for (i = 0; i < 4; i++) for(j = 1; j < 4; j++) tmp[j][i] ^= tmp[j-1][i];
        memcpy(c->key[r], tmp, 16);
    }
}

static void aes128_decrypt_block(AES128Context *c, uint8_t *block) {
    long r = 8;
    uint8_t tmp[16];
    XORBLOCK(block, c->key[10]);
    SUBSTSHIFTROWS(tmp, block);
    XORBLOCK(tmp, c->key[9]);
    INVMIX(tmp, tmp);
    SUBSTSHIFTROWS(block, tmp);
    do {
        XORBLOCK(block, c->key[r]);
        INVMIX(tmp, block);
        SUBSTSHIFTROWS(block, tmp);
    } while (--r);
    XORBLOCK(block, c->key[0]);
}

void aes128_cbc_decrypt(AES128Context *c, uint8_t *mem, int blockcnt, uint8_t *IV) {
    uint8_t tmp[16];
    if (blockcnt & 1) {
        COPYBLOCK(tmp, mem);
        aes128_decrypt_block(c, mem);
        XORBLOCK(mem, IV);
        COPYBLOCK(IV, tmp);
        mem += 16;
    }
    blockcnt >>= 1;
    while (blockcnt-- > 0) {
        COPYBLOCK(tmp, mem);
        aes128_decrypt_block(c, mem);
        XORBLOCK(mem, IV);
        mem += 16;
        COPYBLOCK(IV, mem);
        aes128_decrypt_block(c, mem);
        XORBLOCK(mem, tmp);
        mem += 16;
    }
}
#endif

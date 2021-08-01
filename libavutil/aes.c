/*
 * copyright (c) 2007 Michael Niedermayer <michaelni@gmx.at>
 *
 * some optimization ideas from aes128.c by Reimar Doeffinger
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
 */

#include <string.h>

#include "config.h"
#include "aes.h"
#include "aes_internal.h"
#include "error.h"
#include "intreadwrite.h"
#include "macros.h"
#include "mem.h"

const int av_aes_size= sizeof(AVAES);

struct AVAES *av_aes_alloc(void)
{
    return av_mallocz(sizeof(struct AVAES));
}

static const uint8_t rcon[10] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

static uint8_t     sbox[256];
static uint8_t inv_sbox[256];
#if CONFIG_SMALL
static uint32_t enc_multbl[1][256];
static uint32_t dec_multbl[1][256];
#else
static uint32_t enc_multbl[4][256];
static uint32_t dec_multbl[4][256];
#endif

#if HAVE_BIGENDIAN
#   define ROT(x, s) (((x) >> (s)) | ((x) << (32-(s))))
#else
#   define ROT(x, s) (((x) << (s)) | ((x) >> (32-(s))))
#endif

static inline void addkey(av_aes_block *dst, const av_aes_block *src,
                          const av_aes_block *round_key)
{
    dst->u64[0] = src->u64[0] ^ round_key->u64[0];
    dst->u64[1] = src->u64[1] ^ round_key->u64[1];
}

static inline void addkey_s(av_aes_block *dst, const uint8_t *src,
                            const av_aes_block *round_key)
{
    dst->u64[0] = AV_RN64(src)     ^ round_key->u64[0];
    dst->u64[1] = AV_RN64(src + 8) ^ round_key->u64[1];
}

static inline void addkey_d(uint8_t *dst, const av_aes_block *src,
                            const av_aes_block *round_key)
{
    AV_WN64(dst,     src->u64[0] ^ round_key->u64[0]);
    AV_WN64(dst + 8, src->u64[1] ^ round_key->u64[1]);
}

static void subshift(av_aes_block s0[2], int s, const uint8_t *box)
{
    av_aes_block *s1 = (av_aes_block *) (s0[0].u8 - s);
    av_aes_block *s3 = (av_aes_block *) (s0[0].u8 + s);

    s0[0].u8[ 0] = box[s0[1].u8[ 0]];
    s0[0].u8[ 4] = box[s0[1].u8[ 4]];
    s0[0].u8[ 8] = box[s0[1].u8[ 8]];
    s0[0].u8[12] = box[s0[1].u8[12]];
    s1[0].u8[ 3] = box[s1[1].u8[ 7]];
    s1[0].u8[ 7] = box[s1[1].u8[11]];
    s1[0].u8[11] = box[s1[1].u8[15]];
    s1[0].u8[15] = box[s1[1].u8[ 3]];
    s0[0].u8[ 2] = box[s0[1].u8[10]];
    s0[0].u8[10] = box[s0[1].u8[ 2]];
    s0[0].u8[ 6] = box[s0[1].u8[14]];
    s0[0].u8[14] = box[s0[1].u8[ 6]];
    s3[0].u8[ 1] = box[s3[1].u8[13]];
    s3[0].u8[13] = box[s3[1].u8[ 9]];
    s3[0].u8[ 9] = box[s3[1].u8[ 5]];
    s3[0].u8[ 5] = box[s3[1].u8[ 1]];
}

static inline int mix_core(uint32_t multbl[][256], int a, int b, int c, int d)
{
#if CONFIG_SMALL
    return multbl[0][a] ^ ROT(multbl[0][b], 8) ^ ROT(multbl[0][c], 16) ^ ROT(multbl[0][d], 24);
#else
    return multbl[0][a] ^ multbl[1][b] ^ multbl[2][c] ^ multbl[3][d];
#endif
}

static inline void mix(av_aes_block state[2], uint32_t multbl[][256], int s1, int s3)
{
    uint8_t (*src)[4] = state[1].u8x4;
    state[0].u32[0] = mix_core(multbl, src[0][0], src[s1    ][1], src[2][2], src[s3    ][3]);
    state[0].u32[1] = mix_core(multbl, src[1][0], src[s3 - 1][1], src[3][2], src[s1 - 1][3]);
    state[0].u32[2] = mix_core(multbl, src[2][0], src[s3    ][1], src[0][2], src[s1    ][3]);
    state[0].u32[3] = mix_core(multbl, src[3][0], src[s1 - 1][1], src[1][2], src[s3 - 1][3]);
}

static inline void aes_crypt(AVAES *a, int s, const uint8_t *sbox,
                         uint32_t multbl[][256])
{
    int r;

    for (r = a->rounds - 1; r > 0; r--) {
        mix(a->state, multbl, 3 - s, 1 + s);
        addkey(&a->state[1], &a->state[0], &a->round_key[r]);
    }

    subshift(&a->state[0], s, sbox);
}

static void aes_encrypt(AVAES *a, uint8_t *dst, const uint8_t *src,
                        int count, uint8_t *iv, int rounds)
{
    while (count--) {
        addkey_s(&a->state[1], src, &a->round_key[rounds]);
        if (iv)
            addkey_s(&a->state[1], iv, &a->state[1]);
        aes_crypt(a, 2, sbox, enc_multbl);
        addkey_d(dst, &a->state[0], &a->round_key[0]);
        if (iv)
            memcpy(iv, dst, 16);
        src += 16;
        dst += 16;
    }
}

static void aes_decrypt(AVAES *a, uint8_t *dst, const uint8_t *src,
                        int count, uint8_t *iv, int rounds)
{
    while (count--) {
        addkey_s(&a->state[1], src, &a->round_key[rounds]);
        aes_crypt(a, 0, inv_sbox, dec_multbl);
        if (iv) {
            addkey_s(&a->state[0], iv, &a->state[0]);
            memcpy(iv, src, 16);
        }
        addkey_d(dst, &a->state[0], &a->round_key[0]);
        src += 16;
        dst += 16;
    }
}

void av_aes_crypt(AVAES *a, uint8_t *dst, const uint8_t *src,
                  int count, uint8_t *iv, int decrypt)
{
    a->crypt(a, dst, src, count, iv, a->rounds);
}

static void init_multbl2(uint32_t tbl[][256], const int c[4],
                         const uint8_t *log8, const uint8_t *alog8,
                         const uint8_t *sbox)
{
    int i;

    for (i = 0; i < 256; i++) {
        int x = sbox[i];
        if (x) {
            int k, l, m, n;
            x = log8[x];
            k = alog8[x + log8[c[0]]];
            l = alog8[x + log8[c[1]]];
            m = alog8[x + log8[c[2]]];
            n = alog8[x + log8[c[3]]];
            tbl[0][i] = AV_NE(MKBETAG(k, l, m, n), MKTAG(k, l, m, n));
#if !CONFIG_SMALL
            tbl[1][i] = ROT(tbl[0][i], 8);
            tbl[2][i] = ROT(tbl[0][i], 16);
            tbl[3][i] = ROT(tbl[0][i], 24);
#endif
        }
    }
}

// this is based on the reference AES code by Paulo Barreto and Vincent Rijmen
int av_aes_init(AVAES *a, const uint8_t *key, int key_bits, int decrypt)
{
    int i, j, t, rconpointer = 0;
    uint8_t tk[8][4];
    int KC = key_bits >> 5;
    int rounds = KC + 6;
    uint8_t log8[256];
    uint8_t alog8[512];

    a->crypt = decrypt ? aes_decrypt : aes_encrypt;

    if (!enc_multbl[FF_ARRAY_ELEMS(enc_multbl) - 1][FF_ARRAY_ELEMS(enc_multbl[0]) - 1]) {
        j = 1;
        for (i = 0; i < 255; i++) {
            alog8[i] = alog8[i + 255] = j;
            log8[j] = i;
            j ^= j + j;
            if (j > 255)
                j ^= 0x11B;
        }
        for (i = 0; i < 256; i++) {
            j = i ? alog8[255 - log8[i]] : 0;
            j ^= (j << 1) ^ (j << 2) ^ (j << 3) ^ (j << 4);
            j = (j ^ (j >> 8) ^ 99) & 255;
            inv_sbox[j] = i;
            sbox[i]     = j;
        }
        init_multbl2(dec_multbl, (const int[4]) { 0xe, 0x9, 0xd, 0xb },
                     log8, alog8, inv_sbox);
        init_multbl2(enc_multbl, (const int[4]) { 0x2, 0x1, 0x1, 0x3 },
                     log8, alog8, sbox);
    }

    if (key_bits != 128 && key_bits != 192 && key_bits != 256)
        return AVERROR(EINVAL);

    a->rounds = rounds;

    memcpy(tk, key, KC * 4);
    memcpy(a->round_key[0].u8, key, KC * 4);

    for (t = KC * 4; t < (rounds + 1) * 16; t += KC * 4) {
        for (i = 0; i < 4; i++)
            tk[0][i] ^= sbox[tk[KC - 1][(i + 1) & 3]];
        tk[0][0] ^= rcon[rconpointer++];

        for (j = 1; j < KC; j++) {
            if (KC != 8 || j != KC >> 1)
                for (i = 0; i < 4; i++)
                    tk[j][i] ^= tk[j - 1][i];
            else
                for (i = 0; i < 4; i++)
                    tk[j][i] ^= sbox[tk[j - 1][i]];
        }

        memcpy(a->round_key[0].u8 + t, tk, KC * 4);
    }

    if (decrypt) {
        for (i = 1; i < rounds; i++) {
            av_aes_block tmp[3];
            tmp[2] = a->round_key[i];
            subshift(&tmp[1], 0, sbox);
            mix(tmp, dec_multbl, 1, 3);
            a->round_key[i] = tmp[0];
        }
    } else {
        for (i = 0; i < (rounds + 1) >> 1; i++)
            FFSWAP(av_aes_block, a->round_key[i], a->round_key[rounds - i]);
    }

    return 0;
}


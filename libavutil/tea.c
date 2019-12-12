/*
 * A 32-bit implementation of the TEA algorithm
 * Copyright (c) 2015 Vesselin Bontchev
 *
 * Loosely based on the implementation of David Wheeler and Roger Needham,
 * https://en.wikipedia.org/wiki/Tiny_Encryption_Algorithm#Reference_code
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

#include "avutil.h"
#include "common.h"
#include "intreadwrite.h"
#include "tea.h"

typedef struct AVTEA {
    uint32_t key[16];
    int rounds;
} AVTEA;

struct AVTEA *av_tea_alloc(void)
{
    return av_mallocz(sizeof(struct AVTEA));
}

const int av_tea_size = sizeof(AVTEA);

void av_tea_init(AVTEA *ctx, const uint8_t key[16], int rounds)
{
    int i;

    for (i = 0; i < 4; i++)
        ctx->key[i] = AV_RB32(key + (i << 2));

    ctx->rounds = rounds;
}

static void tea_crypt_ecb(AVTEA *ctx, uint8_t *dst, const uint8_t *src,
                          int decrypt, uint8_t *iv)
{
    uint32_t v0, v1;
    int rounds = ctx->rounds;
    uint32_t k0, k1, k2, k3;
    k0 = ctx->key[0];
    k1 = ctx->key[1];
    k2 = ctx->key[2];
    k3 = ctx->key[3];

    v0 = AV_RB32(src);
    v1 = AV_RB32(src + 4);

    if (decrypt) {
        int i;
        uint32_t delta = 0x9E3779B9U, sum = delta * (rounds / 2);

        for (i = 0; i < rounds / 2; i++) {
            v1 -= ((v0 << 4) + k2) ^ (v0 + sum) ^ ((v0 >> 5) + k3);
            v0 -= ((v1 << 4) + k0) ^ (v1 + sum) ^ ((v1 >> 5) + k1);
            sum -= delta;
        }
        if (iv) {
            v0 ^= AV_RB32(iv);
            v1 ^= AV_RB32(iv + 4);
            memcpy(iv, src, 8);
        }
    } else {
        int i;
        uint32_t sum = 0, delta = 0x9E3779B9U;

        for (i = 0; i < rounds / 2; i++) {
            sum += delta;
            v0 += ((v1 << 4) + k0) ^ (v1 + sum) ^ ((v1 >> 5) + k1);
            v1 += ((v0 << 4) + k2) ^ (v0 + sum) ^ ((v0 >> 5) + k3);
        }
    }

    AV_WB32(dst, v0);
    AV_WB32(dst + 4, v1);
}

void av_tea_crypt(AVTEA *ctx, uint8_t *dst, const uint8_t *src, int count,
                  uint8_t *iv, int decrypt)
{
    int i;

    if (decrypt) {
        while (count--) {
            tea_crypt_ecb(ctx, dst, src, decrypt, iv);

            src   += 8;
            dst   += 8;
        }
    } else {
        while (count--) {
            if (iv) {
                for (i = 0; i < 8; i++)
                    dst[i] = src[i] ^ iv[i];
                tea_crypt_ecb(ctx, dst, dst, decrypt, NULL);
                memcpy(iv, dst, 8);
            } else {
                tea_crypt_ecb(ctx, dst, src, decrypt, NULL);
            }
            src   += 8;
            dst   += 8;
        }
    }
}

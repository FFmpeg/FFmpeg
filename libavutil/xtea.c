/*
 * A 32-bit implementation of the XTEA algorithm
 * Copyright (c) 2012 Samuel Pitoiset
 *
 * loosely based on the implementation of David Wheeler and Roger Needham
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * @brief XTEA 32-bit implementation
 * @author Samuel Pitoiset
 * @ingroup lavu_xtea
 */

#include "avutil.h"
#include "common.h"
#include "intreadwrite.h"
#include "mem.h"
#include "xtea.h"

#if !FF_API_CRYPTO_CONTEXT
struct AVXTEA {
    uint32_t key[16];
};
#endif

AVXTEA *av_xtea_alloc(void)
{
    return av_mallocz(sizeof(struct AVXTEA));
}

void av_xtea_init(AVXTEA *ctx, const uint8_t key[16])
{
    int i;

    for (i = 0; i < 4; i++)
        ctx->key[i] = AV_RB32(key + (i << 2));
}

void av_xtea_le_init(AVXTEA *ctx, const uint8_t key[16])
{
    int i;

    for (i = 0; i < 4; i++)
        ctx->key[i] = AV_RL32(key + (i << 2));
}

static void xtea_crypt_ecb(AVXTEA *ctx, uint8_t *dst, const uint8_t *src,
                           int decrypt, uint8_t *iv)
{
    uint32_t v0, v1;
    int i;

    v0 = AV_RB32(src);
    v1 = AV_RB32(src + 4);

    if (decrypt) {
        uint32_t delta = 0x9E3779B9, sum = delta * 32;

        for (i = 0; i < 32; i++) {
            v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + ctx->key[(sum >> 11) & 3]);
            sum -= delta;
            v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + ctx->key[sum & 3]);
        }
        if (iv) {
            v0 ^= AV_RB32(iv);
            v1 ^= AV_RB32(iv + 4);
            memcpy(iv, src, 8);
        }
    } else {
        uint32_t sum = 0, delta = 0x9E3779B9;

        for (i = 0; i < 32; i++) {
            v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + ctx->key[sum & 3]);
            sum += delta;
            v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + ctx->key[(sum >> 11) & 3]);
        }
    }

    AV_WB32(dst, v0);
    AV_WB32(dst + 4, v1);
}

static void xtea_le_crypt_ecb(AVXTEA *ctx, uint8_t *dst, const uint8_t *src,
                              int decrypt, uint8_t *iv)
{
    uint32_t v0, v1;
    int i;

    v0 = AV_RL32(src);
    v1 = AV_RL32(src + 4);

    if (decrypt) {
        uint32_t delta = 0x9E3779B9, sum = delta * 32;

        for (i = 0; i < 32; i++) {
            v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + ctx->key[(sum >> 11) & 3]);
            sum -= delta;
            v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + ctx->key[sum & 3]);
        }
        if (iv) {
            v0 ^= AV_RL32(iv);
            v1 ^= AV_RL32(iv + 4);
            memcpy(iv, src, 8);
        }
    } else {
        uint32_t sum = 0, delta = 0x9E3779B9;

        for (i = 0; i < 32; i++) {
            v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + ctx->key[sum & 3]);
            sum += delta;
            v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + ctx->key[(sum >> 11) & 3]);
        }
    }

    AV_WL32(dst, v0);
    AV_WL32(dst + 4, v1);
}

static void xtea_crypt(AVXTEA *ctx, uint8_t *dst, const uint8_t *src, int count,
                       uint8_t *iv, int decrypt,
                       void (*crypt)(AVXTEA *, uint8_t *, const uint8_t *, int, uint8_t *))
{
    int i;

    if (decrypt) {
        while (count--) {
            crypt(ctx, dst, src, decrypt, iv);

            src   += 8;
            dst   += 8;
        }
    } else {
        while (count--) {
            if (iv) {
                for (i = 0; i < 8; i++)
                    dst[i] = src[i] ^ iv[i];
                crypt(ctx, dst, dst, decrypt, NULL);
                memcpy(iv, dst, 8);
            } else {
                crypt(ctx, dst, src, decrypt, NULL);
            }
            src   += 8;
            dst   += 8;
        }
    }
}

void av_xtea_crypt(AVXTEA *ctx, uint8_t *dst, const uint8_t *src, int count,
                   uint8_t *iv, int decrypt)
{
    xtea_crypt(ctx, dst, src, count, iv, decrypt, xtea_crypt_ecb);
}

void av_xtea_le_crypt(AVXTEA *ctx, uint8_t *dst, const uint8_t *src, int count,
                      uint8_t *iv, int decrypt)
{
    xtea_crypt(ctx, dst, src, count, iv, decrypt, xtea_le_crypt_ecb);
}

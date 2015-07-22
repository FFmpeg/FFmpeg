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

#ifdef TEST
#include <stdio.h>

#define TEA_NUM_TESTS 4

// https://github.com/logandrews/TeaCrypt/blob/master/tea/tea_test.go
static const uint8_t tea_test_key[TEA_NUM_TESTS][16] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    },
    { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
      0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
    },
    { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
      0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
    }
};

static const uint8_t tea_test_pt[TEA_NUM_TESTS][8] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 },
    { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 },
    { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF }
};

static const uint8_t tea_test_ct[TEA_NUM_TESTS][8] = {
    { 0x41, 0xEA, 0x3A, 0x0A, 0x94, 0xBA, 0xA9, 0x40 },
    { 0x6A, 0x2F, 0x9C, 0xF3, 0xFC, 0xCF, 0x3C, 0x55 },
    { 0xDE, 0xB1, 0xC0, 0xA2, 0x7E, 0x74, 0x5D, 0xB3 },
    { 0x12, 0x6C, 0x6B, 0x92, 0xC0, 0x65, 0x3A, 0x3E }
};

static void test_tea(AVTEA *ctx, uint8_t *dst, const uint8_t *src,
                     const uint8_t *ref, int len, uint8_t *iv, int dir,
                     const char *test)
{
    av_tea_crypt(ctx, dst, src, len, iv, dir);
    if (memcmp(dst, ref, 8*len)) {
        int i;
        printf("%s failed\ngot      ", test);
        for (i = 0; i < 8*len; i++)
            printf("%02x ", dst[i]);
        printf("\nexpected ");
        for (i = 0; i < 8*len; i++)
            printf("%02x ", ref[i]);
        printf("\n");
        exit(1);
    }
}

int main(void)
{
    AVTEA *ctx;
    uint8_t buf[8], iv[8];
    int i;
    static const uint8_t src[32] = "HelloWorldHelloWorldHelloWorld";
    uint8_t ct[32];
    uint8_t pl[32];

    ctx = av_tea_alloc();
    if (!ctx)
        return 1;

    for (i = 0; i < TEA_NUM_TESTS; i++) {
        av_tea_init(ctx, tea_test_key[i], 64);

        test_tea(ctx, buf, tea_test_pt[i], tea_test_ct[i], 1, NULL, 0, "encryption");
        test_tea(ctx, buf, tea_test_ct[i], tea_test_pt[i], 1, NULL, 1, "decryption");

        /* encrypt */
        memcpy(iv, "HALLO123", 8);
        av_tea_crypt(ctx, ct, src, 4, iv, 0);

        /* decrypt into pl */
        memcpy(iv, "HALLO123", 8);
        test_tea(ctx, pl, ct, src, 4, iv, 1, "CBC decryption");

        memcpy(iv, "HALLO123", 8);
        test_tea(ctx, ct, ct, src, 4, iv, 1, "CBC inplace decryption");
    }

    printf("Test encryption/decryption success.\n");
    av_free(ctx);

    return 0;
}

#endif

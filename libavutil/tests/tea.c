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

#include <stdio.h>

#include "libavutil/common.h"
#include "libavutil/tea.h"

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

static void test_tea(struct AVTEA *ctx, uint8_t *dst, const uint8_t *src,
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
    struct AVTEA *ctx;
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

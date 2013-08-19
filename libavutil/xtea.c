/*
 * A 32-bit implementation of the XTEA algorithm
 * Copyright (c) 2012 Samuel Pitoiset
 *
 * loosely based on the implementation of David Wheeler and Roger Needham
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

#include "libavutil/intreadwrite.h"

#include "avutil.h"
#include "common.h"
#include "xtea.h"

void av_xtea_init(AVXTEA *ctx, const uint8_t key[16])
{
    int i;

    for (i = 0; i < 4; i++)
        ctx->key[i] = AV_RB32(key + (i << 2));
}

static void xtea_crypt_ecb(AVXTEA *ctx, uint8_t *dst, const uint8_t *src,
                           int decrypt, uint8_t *iv)
{
    uint32_t v0, v1;
#if !CONFIG_SMALL
    uint32_t k0 = ctx->key[0];
    uint32_t k1 = ctx->key[1];
    uint32_t k2 = ctx->key[2];
    uint32_t k3 = ctx->key[3];
#endif

    v0 = AV_RB32(src);
    v1 = AV_RB32(src + 4);

    if (decrypt) {
#if CONFIG_SMALL
        int i;
        uint32_t delta = 0x9E3779B9U, sum = delta * 32;

        for (i = 0; i < 32; i++) {
            v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + ctx->key[(sum >> 11) & 3]);
            sum -= delta;
            v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + ctx->key[sum & 3]);
        }
#else
#define DSTEP(SUM, K0, K1) \
            v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (SUM + K0); \
            v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (SUM - 0x9E3779B9U + K1)

        DSTEP(0xC6EF3720U, k2, k3);
        DSTEP(0x28B7BD67U, k3, k2);
        DSTEP(0x8A8043AEU, k0, k1);
        DSTEP(0xEC48C9F5U, k1, k0);
        DSTEP(0x4E11503CU, k2, k3);
        DSTEP(0xAFD9D683U, k2, k2);
        DSTEP(0x11A25CCAU, k3, k1);
        DSTEP(0x736AE311U, k0, k0);
        DSTEP(0xD5336958U, k1, k3);
        DSTEP(0x36FBEF9FU, k1, k2);
        DSTEP(0x98C475E6U, k2, k1);
        DSTEP(0xFA8CFC2DU, k3, k0);
        DSTEP(0x5C558274U, k0, k3);
        DSTEP(0xBE1E08BBU, k1, k2);
        DSTEP(0x1FE68F02U, k1, k1);
        DSTEP(0x81AF1549U, k2, k0);
        DSTEP(0xE3779B90U, k3, k3);
        DSTEP(0x454021D7U, k0, k2);
        DSTEP(0xA708A81EU, k1, k1);
        DSTEP(0x08D12E65U, k1, k0);
        DSTEP(0x6A99B4ACU, k2, k3);
        DSTEP(0xCC623AF3U, k3, k2);
        DSTEP(0x2E2AC13AU, k0, k1);
        DSTEP(0x8FF34781U, k0, k0);
        DSTEP(0xF1BBCDC8U, k1, k3);
        DSTEP(0x5384540FU, k2, k2);
        DSTEP(0xB54CDA56U, k3, k1);
        DSTEP(0x1715609DU, k0, k0);
        DSTEP(0x78DDE6E4U, k0, k3);
        DSTEP(0xDAA66D2BU, k1, k2);
        DSTEP(0x3C6EF372U, k2, k1);
        DSTEP(0x9E3779B9U, k3, k0);
#endif
        if (iv) {
            v0 ^= AV_RB32(iv);
            v1 ^= AV_RB32(iv + 4);
            memcpy(iv, src, 8);
        }
    } else {
#if CONFIG_SMALL
        int i;
        uint32_t sum = 0, delta = 0x9E3779B9U;

        for (i = 0; i < 32; i++) {
            v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + ctx->key[sum & 3]);
            sum += delta;
            v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + ctx->key[(sum >> 11) & 3]);
        }
#else
#define ESTEP(SUM, K0, K1) \
            v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (SUM + K0);\
            v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (SUM + 0x9E3779B9U + K1)
        ESTEP(0x00000000U, k0, k3);
        ESTEP(0x9E3779B9U, k1, k2);
        ESTEP(0x3C6EF372U, k2, k1);
        ESTEP(0xDAA66D2BU, k3, k0);
        ESTEP(0x78DDE6E4U, k0, k0);
        ESTEP(0x1715609DU, k1, k3);
        ESTEP(0xB54CDA56U, k2, k2);
        ESTEP(0x5384540FU, k3, k1);
        ESTEP(0xF1BBCDC8U, k0, k0);
        ESTEP(0x8FF34781U, k1, k0);
        ESTEP(0x2E2AC13AU, k2, k3);
        ESTEP(0xCC623AF3U, k3, k2);
        ESTEP(0x6A99B4ACU, k0, k1);
        ESTEP(0x08D12E65U, k1, k1);
        ESTEP(0xA708A81EU, k2, k0);
        ESTEP(0x454021D7U, k3, k3);
        ESTEP(0xE3779B90U, k0, k2);
        ESTEP(0x81AF1549U, k1, k1);
        ESTEP(0x1FE68F02U, k2, k1);
        ESTEP(0xBE1E08BBU, k3, k0);
        ESTEP(0x5C558274U, k0, k3);
        ESTEP(0xFA8CFC2DU, k1, k2);
        ESTEP(0x98C475E6U, k2, k1);
        ESTEP(0x36FBEF9FU, k3, k1);
        ESTEP(0xD5336958U, k0, k0);
        ESTEP(0x736AE311U, k1, k3);
        ESTEP(0x11A25CCAU, k2, k2);
        ESTEP(0xAFD9D683U, k3, k2);
        ESTEP(0x4E11503CU, k0, k1);
        ESTEP(0xEC48C9F5U, k1, k0);
        ESTEP(0x8A8043AEU, k2, k3);
        ESTEP(0x28B7BD67U, k3, k2);
#endif
    }

    AV_WB32(dst, v0);
    AV_WB32(dst + 4, v1);
}

void av_xtea_crypt(AVXTEA *ctx, uint8_t *dst, const uint8_t *src, int count,
                   uint8_t *iv, int decrypt)
{
    int i;

    if (decrypt) {
        while (count--) {
            xtea_crypt_ecb(ctx, dst, src, decrypt, iv);

            src   += 8;
            dst   += 8;
        }
    } else {
        while (count--) {
            if (iv) {
                for (i = 0; i < 8; i++)
                    dst[i] = src[i] ^ iv[i];
                xtea_crypt_ecb(ctx, dst, dst, decrypt, NULL);
                memcpy(iv, dst, 8);
            } else {
                xtea_crypt_ecb(ctx, dst, src, decrypt, NULL);
            }
            src   += 8;
            dst   += 8;
        }
    }
}

#ifdef TEST
#include <stdio.h>

#define XTEA_NUM_TESTS 6

static const uint8_t xtea_test_key[XTEA_NUM_TESTS][16] = {
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f },
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f },
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
};

static const uint8_t xtea_test_pt[XTEA_NUM_TESTS][8] = {
    { 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48 },
    { 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41 },
    { 0x5a, 0x5b, 0x6e, 0x27, 0x89, 0x48, 0xd7, 0x7f },
    { 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48 },
    { 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41 },
    { 0x70, 0xe1, 0x22, 0x5d, 0x6e, 0x4e, 0x76, 0x55 }
};

static const uint8_t xtea_test_ct[XTEA_NUM_TESTS][8] = {
    { 0x49, 0x7d, 0xf3, 0xd0, 0x72, 0x61, 0x2c, 0xb5 },
    { 0xe7, 0x8f, 0x2d, 0x13, 0x74, 0x43, 0x41, 0xd8 },
    { 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41 },
    { 0xa0, 0x39, 0x05, 0x89, 0xf8, 0xb8, 0xef, 0xa5 },
    { 0xed, 0x23, 0x37, 0x5a, 0x82, 0x1a, 0x8c, 0x2d },
    { 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41 }
};

static void test_xtea(AVXTEA *ctx, uint8_t *dst, const uint8_t *src,
                      const uint8_t *ref, int len, uint8_t *iv, int dir,
                      const char *test)
{
    av_xtea_crypt(ctx, dst, src, len, iv, dir);
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
    AVXTEA ctx;
    uint8_t buf[8], iv[8];
    int i;
    static const uint8_t src[32] = "HelloWorldHelloWorldHelloWorld";
    uint8_t ct[32];
    uint8_t pl[32];

    for (i = 0; i < XTEA_NUM_TESTS; i++) {
        av_xtea_init(&ctx, xtea_test_key[i]);

        test_xtea(&ctx, buf, xtea_test_pt[i], xtea_test_ct[i], 1, NULL, 0, "encryption");
        test_xtea(&ctx, buf, xtea_test_ct[i], xtea_test_pt[i], 1, NULL, 1, "decryption");

        /* encrypt */
        memcpy(iv, "HALLO123", 8);
        av_xtea_crypt(&ctx, ct, src, 4, iv, 0);

        /* decrypt into pl */
        memcpy(iv, "HALLO123", 8);
        test_xtea(&ctx, pl, ct, src, 4, iv, 1, "CBC decryption");

        memcpy(iv, "HALLO123", 8);
        test_xtea(&ctx, ct, ct, src, 4, iv, 1, "CBC inplace decryption");
    }

    printf("Test encryption/decryption success.\n");

    return 0;
}

#endif

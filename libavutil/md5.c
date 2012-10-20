/*
 * Copyright (C) 2006 Michael Niedermayer (michaelni@gmx.at)
 * Copyright (C) 2003-2005 by Christopher R. Hertel (crh@ubiqx.mn.org)
 *
 * References:
 *  IETF RFC 1321: The MD5 Message-Digest Algorithm
 *       Ron Rivest. IETF, April, 1992
 *
 * based on http://ubiqx.org/libcifs/source/Auth/MD5.c
 *          from Christopher R. Hertel (crh@ubiqx.mn.org)
 * Simplified, cleaned and IMO redundant comments removed by michael.
 *
 * If you use gcc, then version 4.1 or later and -fomit-frame-pointer is
 * strongly recommended.
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

#include <stdint.h>
#include "bswap.h"
#include "intreadwrite.h"
#include "md5.h"
#include "mem.h"

typedef struct AVMD5{
    uint64_t len;
    uint8_t  block[64];
    uint32_t ABCD[4];
} AVMD5;

#if FF_API_CONTEXT_SIZE
const int av_md5_size = sizeof(AVMD5);
#endif

struct AVMD5 *av_md5_alloc(void)
{
    return av_mallocz(sizeof(struct AVMD5));
}

static const uint8_t S[4][4] = {
    { 7, 12, 17, 22 },  /* round 1 */
    { 5,  9, 14, 20 },  /* round 2 */
    { 4, 11, 16, 23 },  /* round 3 */
    { 6, 10, 15, 21 }   /* round 4 */
};

static const uint32_t T[64] = { // T[i]= fabs(sin(i+1)<<32)
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,   /* round 1 */
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,

    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,   /* round 2 */
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,

    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,   /* round 3 */
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,

    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,   /* round 4 */
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

#define CORE(i, a, b, c, d) do {                                        \
        t = S[i >> 4][i & 3];                                           \
        a += T[i];                                                      \
                                                                        \
        if (i < 32) {                                                   \
            if (i < 16) a += (d ^ (b & (c ^ d))) + X[       i  & 15];   \
            else        a += (c ^ (d & (c ^ b))) + X[(1 + 5*i) & 15];   \
        } else {                                                        \
            if (i < 48) a += (b ^ c ^ d)         + X[(5 + 3*i) & 15];   \
            else        a += (c ^ (b | ~d))      + X[(    7*i) & 15];   \
        }                                                               \
        a = b + (a << t | a >> (32 - t));                               \
    } while (0)

static void body(uint32_t ABCD[4], uint32_t X[16])
{
    int t;
    int i av_unused;
    unsigned int a = ABCD[3];
    unsigned int b = ABCD[2];
    unsigned int c = ABCD[1];
    unsigned int d = ABCD[0];

#if HAVE_BIGENDIAN
    for (i = 0; i < 16; i++)
        X[i] = av_bswap32(X[i]);
#endif

#if CONFIG_SMALL
    for (i = 0; i < 64; i++) {
        CORE(i, a, b, c, d);
        t = d;
        d = c;
        c = b;
        b = a;
        a = t;
    }
#else
#define CORE2(i)                                                        \
    CORE( i,   a,b,c,d); CORE((i+1),d,a,b,c);                           \
    CORE((i+2),c,d,a,b); CORE((i+3),b,c,d,a)
#define CORE4(i) CORE2(i); CORE2((i+4)); CORE2((i+8)); CORE2((i+12))
    CORE4(0); CORE4(16); CORE4(32); CORE4(48);
#endif

    ABCD[0] += d;
    ABCD[1] += c;
    ABCD[2] += b;
    ABCD[3] += a;
}

void av_md5_init(AVMD5 *ctx)
{
    ctx->len     = 0;

    ctx->ABCD[0] = 0x10325476;
    ctx->ABCD[1] = 0x98badcfe;
    ctx->ABCD[2] = 0xefcdab89;
    ctx->ABCD[3] = 0x67452301;
}

void av_md5_update(AVMD5 *ctx, const uint8_t *src, const int len)
{
    int i, j;

    j = ctx->len & 63;
    ctx->len += len;

    for (i = 0; i < len; i++) {
        ctx->block[j++] = src[i];
        if (j == 64) {
            body(ctx->ABCD, (uint32_t *) ctx->block);
            j = 0;
        }
    }
}

void av_md5_final(AVMD5 *ctx, uint8_t *dst)
{
    int i;
    uint64_t finalcount = av_le2ne64(ctx->len << 3);

    av_md5_update(ctx, "\200", 1);
    while ((ctx->len & 63) != 56)
        av_md5_update(ctx, "", 1);

    av_md5_update(ctx, (uint8_t *)&finalcount, 8);

    for (i = 0; i < 4; i++)
        AV_WL32(dst + 4*i, ctx->ABCD[3 - i]);
}

void av_md5_sum(uint8_t *dst, const uint8_t *src, const int len)
{
    AVMD5 ctx;

    av_md5_init(&ctx);
    av_md5_update(&ctx, src, len);
    av_md5_final(&ctx, dst);
}

#ifdef TEST
#include <stdio.h>

static void print_md5(uint8_t *md5)
{
    int i;
    for (i = 0; i < 16; i++)
        printf("%02x", md5[i]);
    printf("\n");
}

int main(void){
    uint8_t md5val[16];
    int i;
    uint8_t in[1000];

    for (i = 0; i < 1000; i++)
        in[i] = i * i;
    av_md5_sum(md5val, in, 1000); print_md5(md5val);
    av_md5_sum(md5val, in,   63); print_md5(md5val);
    av_md5_sum(md5val, in,   64); print_md5(md5val);
    av_md5_sum(md5val, in,   65); print_md5(md5val);
    for (i = 0; i < 1000; i++)
        in[i] = i % 127;
    av_md5_sum(md5val, in,  999); print_md5(md5val);

    return 0;
}
#endif

/*
 * Copyright (C) 2007 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (C) 2009 Konstantin Shishkov
 * based on public domain SHA-1 code by Steve Reid <steve@edmweb.com>
 * and on BSD-licensed SHA-2 code by Aaron D. Gifford
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
#include "avutil.h"
#include "bswap.h"
#include "sha.h"
#include "intreadwrite.h"
#include "mem.h"

/** hash context */
typedef struct AVSHA {
    uint8_t  digest_len;  ///< digest length in 32-bit words
    uint64_t count;       ///< number of bytes in buffer
    uint8_t  buffer[64];  ///< 512-bit buffer of input values used in hash updating
    uint32_t state[8];    ///< current hash value
    /** function used to update hash for 512-bit input block */
    void     (*transform)(uint32_t *state, const uint8_t buffer[64]);
} AVSHA;

const int av_sha_size = sizeof(AVSHA);

struct AVSHA *av_sha_alloc(void)
{
    return av_mallocz(sizeof(struct AVSHA));
}

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

/* (R0+R1), R2, R3, R4 are the different operations used in SHA1 */
#define blk0(i) (block[i] = AV_RB32(buffer + 4 * (i)))
#define blk(i)  (block[i] = rol(block[i-3] ^ block[i-8] ^ block[i-14] ^ block[i-16], 1))

#define R0(v,w,x,y,z,i) z += ((w&(x^y))^y)     + blk0(i) + 0x5A827999 + rol(v, 5); w = rol(w, 30);
#define R1(v,w,x,y,z,i) z += ((w&(x^y))^y)     + blk (i) + 0x5A827999 + rol(v, 5); w = rol(w, 30);
#define R2(v,w,x,y,z,i) z += ( w^x     ^y)     + blk (i) + 0x6ED9EBA1 + rol(v, 5); w = rol(w, 30);
#define R3(v,w,x,y,z,i) z += (((w|x)&y)|(w&x)) + blk (i) + 0x8F1BBCDC + rol(v, 5); w = rol(w, 30);
#define R4(v,w,x,y,z,i) z += ( w^x     ^y)     + blk (i) + 0xCA62C1D6 + rol(v, 5); w = rol(w, 30);

/* Hash a single 512-bit block. This is the core of the algorithm. */

static void sha1_transform(uint32_t state[5], const uint8_t buffer[64])
{
    uint32_t block[80];
    unsigned int i, a, b, c, d, e;

    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
#if CONFIG_SMALL
    for (i = 0; i < 80; i++) {
        int t;
        if (i < 16)
            t = AV_RB32(buffer + 4 * i);
        else
            t = rol(block[i-3] ^ block[i-8] ^ block[i-14] ^ block[i-16], 1);
        block[i] = t;
        t += e + rol(a, 5);
        if (i < 40) {
            if (i < 20)
                t += ((b&(c^d))^d)     + 0x5A827999;
            else
                t += ( b^c     ^d)     + 0x6ED9EBA1;
        } else {
            if (i < 60)
                t += (((b|c)&d)|(b&c)) + 0x8F1BBCDC;
            else
                t += ( b^c     ^d)     + 0xCA62C1D6;
        }
        e = d;
        d = c;
        c = rol(b, 30);
        b = a;
        a = t;
    }
#else
    for (i = 0; i < 15; i += 5) {
        R0(a, b, c, d, e, 0 + i);
        R0(e, a, b, c, d, 1 + i);
        R0(d, e, a, b, c, 2 + i);
        R0(c, d, e, a, b, 3 + i);
        R0(b, c, d, e, a, 4 + i);
    }
    R0(a, b, c, d, e, 15);
    R1(e, a, b, c, d, 16);
    R1(d, e, a, b, c, 17);
    R1(c, d, e, a, b, 18);
    R1(b, c, d, e, a, 19);
    for (i = 20; i < 40; i += 5) {
        R2(a, b, c, d, e, 0 + i);
        R2(e, a, b, c, d, 1 + i);
        R2(d, e, a, b, c, 2 + i);
        R2(c, d, e, a, b, 3 + i);
        R2(b, c, d, e, a, 4 + i);
    }
    for (; i < 60; i += 5) {
        R3(a, b, c, d, e, 0 + i);
        R3(e, a, b, c, d, 1 + i);
        R3(d, e, a, b, c, 2 + i);
        R3(c, d, e, a, b, 3 + i);
        R3(b, c, d, e, a, 4 + i);
    }
    for (; i < 80; i += 5) {
        R4(a, b, c, d, e, 0 + i);
        R4(e, a, b, c, d, 1 + i);
        R4(d, e, a, b, c, 2 + i);
        R4(c, d, e, a, b, 3 + i);
        R4(b, c, d, e, a, 4 + i);
    }
#endif
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};


#define Ch(x,y,z)   (((x) & ((y) ^ (z))) ^ (z))
#define Maj(x,y,z)  ((((x) | (y)) & (z)) | ((x) & (y)))

#define Sigma0_256(x)   (rol((x), 30) ^ rol((x), 19) ^ rol((x), 10))
#define Sigma1_256(x)   (rol((x), 26) ^ rol((x), 21) ^ rol((x),  7))
#define sigma0_256(x)   (rol((x), 25) ^ rol((x), 14) ^ ((x) >> 3))
#define sigma1_256(x)   (rol((x), 15) ^ rol((x), 13) ^ ((x) >> 10))

#undef blk
#define blk(i)  (block[i] = block[i - 16] + sigma0_256(block[i - 15]) + \
                            sigma1_256(block[i - 2]) + block[i - 7])

#define ROUND256(a,b,c,d,e,f,g,h)   \
    T1 += (h) + Sigma1_256(e) + Ch((e), (f), (g)) + K256[i]; \
    (d) += T1; \
    (h) = T1 + Sigma0_256(a) + Maj((a), (b), (c)); \
    i++

#define ROUND256_0_TO_15(a,b,c,d,e,f,g,h)   \
    T1 = blk0(i); \
    ROUND256(a,b,c,d,e,f,g,h)

#define ROUND256_16_TO_63(a,b,c,d,e,f,g,h)   \
    T1 = blk(i); \
    ROUND256(a,b,c,d,e,f,g,h)

static void sha256_transform(uint32_t *state, const uint8_t buffer[64])
{
    unsigned int i, a, b, c, d, e, f, g, h;
    uint32_t block[64];
    uint32_t T1;

    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    f = state[5];
    g = state[6];
    h = state[7];
#if CONFIG_SMALL
    for (i = 0; i < 64; i++) {
        uint32_t T2;
        if (i < 16)
            T1 = blk0(i);
        else
            T1 = blk(i);
        T1 += h + Sigma1_256(e) + Ch(e, f, g) + K256[i];
        T2 = Sigma0_256(a) + Maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
    }
#else
    for (i = 0; i < 16 - 7;) {
        ROUND256_0_TO_15(a, b, c, d, e, f, g, h);
        ROUND256_0_TO_15(h, a, b, c, d, e, f, g);
        ROUND256_0_TO_15(g, h, a, b, c, d, e, f);
        ROUND256_0_TO_15(f, g, h, a, b, c, d, e);
        ROUND256_0_TO_15(e, f, g, h, a, b, c, d);
        ROUND256_0_TO_15(d, e, f, g, h, a, b, c);
        ROUND256_0_TO_15(c, d, e, f, g, h, a, b);
        ROUND256_0_TO_15(b, c, d, e, f, g, h, a);
    }

    for (; i < 64 - 7;) {
        ROUND256_16_TO_63(a, b, c, d, e, f, g, h);
        ROUND256_16_TO_63(h, a, b, c, d, e, f, g);
        ROUND256_16_TO_63(g, h, a, b, c, d, e, f);
        ROUND256_16_TO_63(f, g, h, a, b, c, d, e);
        ROUND256_16_TO_63(e, f, g, h, a, b, c, d);
        ROUND256_16_TO_63(d, e, f, g, h, a, b, c);
        ROUND256_16_TO_63(c, d, e, f, g, h, a, b);
        ROUND256_16_TO_63(b, c, d, e, f, g, h, a);
    }
#endif
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}


int av_sha_init(AVSHA* ctx, int bits)
{
    ctx->digest_len = bits >> 5;
    switch (bits) {
    case 160: // SHA-1
        ctx->state[0] = 0x67452301;
        ctx->state[1] = 0xEFCDAB89;
        ctx->state[2] = 0x98BADCFE;
        ctx->state[3] = 0x10325476;
        ctx->state[4] = 0xC3D2E1F0;
        ctx->transform = sha1_transform;
        break;
    case 224: // SHA-224
        ctx->state[0] = 0xC1059ED8;
        ctx->state[1] = 0x367CD507;
        ctx->state[2] = 0x3070DD17;
        ctx->state[3] = 0xF70E5939;
        ctx->state[4] = 0xFFC00B31;
        ctx->state[5] = 0x68581511;
        ctx->state[6] = 0x64F98FA7;
        ctx->state[7] = 0xBEFA4FA4;
        ctx->transform = sha256_transform;
        break;
    case 256: // SHA-256
        ctx->state[0] = 0x6A09E667;
        ctx->state[1] = 0xBB67AE85;
        ctx->state[2] = 0x3C6EF372;
        ctx->state[3] = 0xA54FF53A;
        ctx->state[4] = 0x510E527F;
        ctx->state[5] = 0x9B05688C;
        ctx->state[6] = 0x1F83D9AB;
        ctx->state[7] = 0x5BE0CD19;
        ctx->transform = sha256_transform;
        break;
    default:
        return -1;
    }
    ctx->count = 0;
    return 0;
}

void av_sha_update(AVSHA* ctx, const uint8_t* data, unsigned int len)
{
    unsigned int i, j;

    j = ctx->count & 63;
    ctx->count += len;
#if CONFIG_SMALL
    for (i = 0; i < len; i++) {
        ctx->buffer[j++] = data[i];
        if (64 == j) {
            ctx->transform(ctx->state, ctx->buffer);
            j = 0;
        }
    }
#else
    if ((j + len) > 63) {
        memcpy(&ctx->buffer[j], data, (i = 64 - j));
        ctx->transform(ctx->state, ctx->buffer);
        for (; i + 63 < len; i += 64)
            ctx->transform(ctx->state, &data[i]);
        j = 0;
    } else
        i = 0;
    memcpy(&ctx->buffer[j], &data[i], len - i);
#endif
}

void av_sha_final(AVSHA* ctx, uint8_t *digest)
{
    int i;
    uint64_t finalcount = av_be2ne64(ctx->count << 3);

    av_sha_update(ctx, "\200", 1);
    while ((ctx->count & 63) != 56)
        av_sha_update(ctx, "", 1);
    av_sha_update(ctx, (uint8_t *)&finalcount, 8); /* Should cause a transform() */
    for (i = 0; i < ctx->digest_len; i++)
        AV_WB32(digest + i*4, ctx->state[i]);
}

#ifdef TEST
#include <stdio.h>

int main(void)
{
    int i, j, k;
    AVSHA ctx;
    unsigned char digest[32];
    const int lengths[3] = { 160, 224, 256 };

    for (j = 0; j < 3; j++) {
        printf("Testing SHA-%d\n", lengths[j]);
        for (k = 0; k < 3; k++) {
            av_sha_init(&ctx, lengths[j]);
            if (k == 0)
                av_sha_update(&ctx, "abc", 3);
            else if (k == 1)
                av_sha_update(&ctx, "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56);
            else
                for (i = 0; i < 1000*1000; i++)
                    av_sha_update(&ctx, "a", 1);
            av_sha_final(&ctx, digest);
            for (i = 0; i < lengths[j] >> 3; i++)
                printf("%02X", digest[i]);
            putchar('\n');
        }
        switch (j) {
        case 0:
            //test vectors (from FIPS PUB 180-1)
            printf("A9993E36 4706816A BA3E2571 7850C26C 9CD0D89D\n"
                   "84983E44 1C3BD26E BAAE4AA1 F95129E5 E54670F1\n"
                   "34AA973C D4C4DAA4 F61EEB2B DBAD2731 6534016F\n");
            break;
        case 1:
            //test vectors (from FIPS PUB 180-2 Appendix A)
            printf("23097d22 3405d822 8642a477 bda255b3 2aadbce4 bda0b3f7 e36c9da7\n"
                   "75388b16 512776cc 5dba5da1 fd890150 b0c6455c b4f58b19 52522525\n"
                   "20794655 980c91d8 bbb4c1ea 97618a4b f03f4258 1948b2ee 4ee7ad67\n");
            break;
        case 2:
            //test vectors (from FIPS PUB 180-2)
            printf("ba7816bf 8f01cfea 414140de 5dae2223 b00361a3 96177a9c b410ff61 f20015ad\n"
                   "248d6a61 d20638b8 e5c02693 0c3e6039 a33ce459 64ff2167 f6ecedd4 19db06c1\n"
                   "cdc76e5c 9914fb92 81a1c7e2 84d73e67 f1809a48 a497200e 046d39cc c7112cd0\n");
            break;
        }
    }

    return 0;
}
#endif

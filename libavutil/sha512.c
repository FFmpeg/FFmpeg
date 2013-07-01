/*
 * Copyright (C) 2007 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (C) 2009 Konstantin Shishkov
 * Copyright (C) 2013 James Almer
 * based on BSD-licensed SHA-2 code by Aaron D. Gifford
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

#include "attributes.h"
#include "avutil.h"
#include "bswap.h"
#include "sha512.h"
#include "intreadwrite.h"
#include "mem.h"

/** hash context */
typedef struct AVSHA512 {
    uint8_t  digest_len;  ///< digest length in 64-bit words
    uint64_t count;       ///< number of bytes in buffer
    uint8_t  buffer[128]; ///< 1024-bit buffer of input values used in hash updating
    uint64_t state[8];    ///< current hash value
} AVSHA512;

const int av_sha512_size = sizeof(AVSHA512);

struct AVSHA512 *av_sha512_alloc(void)
{
    return av_mallocz(sizeof(struct AVSHA512));
}

static const uint64_t K512[80] = {
    UINT64_C(0x428a2f98d728ae22),  UINT64_C(0x7137449123ef65cd),
    UINT64_C(0xb5c0fbcfec4d3b2f),  UINT64_C(0xe9b5dba58189dbbc),
    UINT64_C(0x3956c25bf348b538),  UINT64_C(0x59f111f1b605d019),
    UINT64_C(0x923f82a4af194f9b),  UINT64_C(0xab1c5ed5da6d8118),
    UINT64_C(0xd807aa98a3030242),  UINT64_C(0x12835b0145706fbe),
    UINT64_C(0x243185be4ee4b28c),  UINT64_C(0x550c7dc3d5ffb4e2),
    UINT64_C(0x72be5d74f27b896f),  UINT64_C(0x80deb1fe3b1696b1),
    UINT64_C(0x9bdc06a725c71235),  UINT64_C(0xc19bf174cf692694),
    UINT64_C(0xe49b69c19ef14ad2),  UINT64_C(0xefbe4786384f25e3),
    UINT64_C(0x0fc19dc68b8cd5b5),  UINT64_C(0x240ca1cc77ac9c65),
    UINT64_C(0x2de92c6f592b0275),  UINT64_C(0x4a7484aa6ea6e483),
    UINT64_C(0x5cb0a9dcbd41fbd4),  UINT64_C(0x76f988da831153b5),
    UINT64_C(0x983e5152ee66dfab),  UINT64_C(0xa831c66d2db43210),
    UINT64_C(0xb00327c898fb213f),  UINT64_C(0xbf597fc7beef0ee4),
    UINT64_C(0xc6e00bf33da88fc2),  UINT64_C(0xd5a79147930aa725),
    UINT64_C(0x06ca6351e003826f),  UINT64_C(0x142929670a0e6e70),
    UINT64_C(0x27b70a8546d22ffc),  UINT64_C(0x2e1b21385c26c926),
    UINT64_C(0x4d2c6dfc5ac42aed),  UINT64_C(0x53380d139d95b3df),
    UINT64_C(0x650a73548baf63de),  UINT64_C(0x766a0abb3c77b2a8),
    UINT64_C(0x81c2c92e47edaee6),  UINT64_C(0x92722c851482353b),
    UINT64_C(0xa2bfe8a14cf10364),  UINT64_C(0xa81a664bbc423001),
    UINT64_C(0xc24b8b70d0f89791),  UINT64_C(0xc76c51a30654be30),
    UINT64_C(0xd192e819d6ef5218),  UINT64_C(0xd69906245565a910),
    UINT64_C(0xf40e35855771202a),  UINT64_C(0x106aa07032bbd1b8),
    UINT64_C(0x19a4c116b8d2d0c8),  UINT64_C(0x1e376c085141ab53),
    UINT64_C(0x2748774cdf8eeb99),  UINT64_C(0x34b0bcb5e19b48a8),
    UINT64_C(0x391c0cb3c5c95a63),  UINT64_C(0x4ed8aa4ae3418acb),
    UINT64_C(0x5b9cca4f7763e373),  UINT64_C(0x682e6ff3d6b2b8a3),
    UINT64_C(0x748f82ee5defb2fc),  UINT64_C(0x78a5636f43172f60),
    UINT64_C(0x84c87814a1f0ab72),  UINT64_C(0x8cc702081a6439ec),
    UINT64_C(0x90befffa23631e28),  UINT64_C(0xa4506cebde82bde9),
    UINT64_C(0xbef9a3f7b2c67915),  UINT64_C(0xc67178f2e372532b),
    UINT64_C(0xca273eceea26619c),  UINT64_C(0xd186b8c721c0c207),
    UINT64_C(0xeada7dd6cde0eb1e),  UINT64_C(0xf57d4f7fee6ed178),
    UINT64_C(0x06f067aa72176fba),  UINT64_C(0x0a637dc5a2c898a6),
    UINT64_C(0x113f9804bef90dae),  UINT64_C(0x1b710b35131c471b),
    UINT64_C(0x28db77f523047d84),  UINT64_C(0x32caab7b40c72493),
    UINT64_C(0x3c9ebe0a15c9bebc),  UINT64_C(0x431d67c49c100d4c),
    UINT64_C(0x4cc5d4becb3e42b6),  UINT64_C(0x597f299cfc657e2a),
    UINT64_C(0x5fcb6fab3ad6faec),  UINT64_C(0x6c44198c4a475817),
};

#define ror(value, bits) (((value) >> (bits)) | ((value) << (64 - (bits))))

#define Ch(x,y,z)   (((x) & ((y) ^ (z))) ^ (z))
#define Maj(z,y,x)  ((((x) | (y)) & (z)) | ((x) & (y)))

#define Sigma0_512(x)   (ror((x), 28) ^ ror((x), 34) ^ ror((x), 39))
#define Sigma1_512(x)   (ror((x), 14) ^ ror((x), 18) ^ ror((x), 41))
#define sigma0_512(x)   (ror((x),  1) ^ ror((x),  8) ^ ((x) >> 7))
#define sigma1_512(x)   (ror((x), 19) ^ ror((x), 61) ^ ((x) >> 6))

#define blk0(i) (block[i] = AV_RB64(buffer + 8 * (i)))
#define blk(i)  (block[i] = block[i - 16] + sigma0_512(block[i - 15]) + \
                            sigma1_512(block[i - 2]) + block[i - 7])

#define ROUND512(a,b,c,d,e,f,g,h)   \
    T1 += (h) + Sigma1_512(e) + Ch((e), (f), (g)) + K512[i]; \
    (d) += T1; \
    (h) = T1 + Sigma0_512(a) + Maj((a), (b), (c)); \
    i++

#define ROUND512_0_TO_15(a,b,c,d,e,f,g,h)   \
    T1 = blk0(i); \
    ROUND512(a,b,c,d,e,f,g,h)

#define ROUND512_16_TO_80(a,b,c,d,e,f,g,h)   \
    T1 = blk(i); \
    ROUND512(a,b,c,d,e,f,g,h)

static void sha512_transform(uint64_t *state, const uint8_t buffer[128])
{
    uint64_t a, b, c, d, e, f, g, h;
    uint64_t block[80];
    uint64_t T1;
    int i;

    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    f = state[5];
    g = state[6];
    h = state[7];
#if CONFIG_SMALL
    for (i = 0; i < 80; i++) {
        uint64_t T2;
        if (i < 16)
            T1 = blk0(i);
        else
            T1 = blk(i);
        T1 += h + Sigma1_512(e) + Ch(e, f, g) + K512[i];
        T2 = Sigma0_512(a) + Maj(a, b, c);
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
        ROUND512_0_TO_15(a, b, c, d, e, f, g, h);
        ROUND512_0_TO_15(h, a, b, c, d, e, f, g);
        ROUND512_0_TO_15(g, h, a, b, c, d, e, f);
        ROUND512_0_TO_15(f, g, h, a, b, c, d, e);
        ROUND512_0_TO_15(e, f, g, h, a, b, c, d);
        ROUND512_0_TO_15(d, e, f, g, h, a, b, c);
        ROUND512_0_TO_15(c, d, e, f, g, h, a, b);
        ROUND512_0_TO_15(b, c, d, e, f, g, h, a);
    }

    for (; i < 80 - 7;) {
        ROUND512_16_TO_80(a, b, c, d, e, f, g, h);
        ROUND512_16_TO_80(h, a, b, c, d, e, f, g);
        ROUND512_16_TO_80(g, h, a, b, c, d, e, f);
        ROUND512_16_TO_80(f, g, h, a, b, c, d, e);
        ROUND512_16_TO_80(e, f, g, h, a, b, c, d);
        ROUND512_16_TO_80(d, e, f, g, h, a, b, c);
        ROUND512_16_TO_80(c, d, e, f, g, h, a, b);
        ROUND512_16_TO_80(b, c, d, e, f, g, h, a);
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


av_cold int av_sha512_init(AVSHA512 *ctx, int bits)
{
    ctx->digest_len = bits >> 6;
    switch (bits) {
    case 224: // SHA-512/224
        ctx->state[0] = UINT64_C(0x8C3D37C819544DA2);
        ctx->state[1] = UINT64_C(0x73E1996689DCD4D6);
        ctx->state[2] = UINT64_C(0x1DFAB7AE32FF9C82);
        ctx->state[3] = UINT64_C(0x679DD514582F9FCF);
        ctx->state[4] = UINT64_C(0x0F6D2B697BD44DA8);
        ctx->state[5] = UINT64_C(0x77E36F7304C48942);
        ctx->state[6] = UINT64_C(0x3F9D85A86A1D36C8);
        ctx->state[7] = UINT64_C(0x1112E6AD91D692A1);
        break;
    case 256: // SHA-512/256
        ctx->state[0] = UINT64_C(0x22312194FC2BF72C);
        ctx->state[1] = UINT64_C(0x9F555FA3C84C64C2);
        ctx->state[2] = UINT64_C(0x2393B86B6F53B151);
        ctx->state[3] = UINT64_C(0x963877195940EABD);
        ctx->state[4] = UINT64_C(0x96283EE2A88EFFE3);
        ctx->state[5] = UINT64_C(0xBE5E1E2553863992);
        ctx->state[6] = UINT64_C(0x2B0199FC2C85B8AA);
        ctx->state[7] = UINT64_C(0x0EB72DDC81C52CA2);
        break;
    case 384: // SHA-384
        ctx->state[0] = UINT64_C(0xCBBB9D5DC1059ED8);
        ctx->state[1] = UINT64_C(0x629A292A367CD507);
        ctx->state[2] = UINT64_C(0x9159015A3070DD17);
        ctx->state[3] = UINT64_C(0x152FECD8F70E5939);
        ctx->state[4] = UINT64_C(0x67332667FFC00B31);
        ctx->state[5] = UINT64_C(0x8EB44A8768581511);
        ctx->state[6] = UINT64_C(0xDB0C2E0D64F98FA7);
        ctx->state[7] = UINT64_C(0x47B5481DBEFA4FA4);
        break;
    case 512: // SHA-512
        ctx->state[0] = UINT64_C(0x6A09E667F3BCC908);
        ctx->state[1] = UINT64_C(0xBB67AE8584CAA73B);
        ctx->state[2] = UINT64_C(0x3C6EF372FE94F82B);
        ctx->state[3] = UINT64_C(0xA54FF53A5F1D36F1);
        ctx->state[4] = UINT64_C(0x510E527FADE682D1);
        ctx->state[5] = UINT64_C(0x9B05688C2B3E6C1F);
        ctx->state[6] = UINT64_C(0x1F83D9ABFB41BD6B);
        ctx->state[7] = UINT64_C(0x5BE0CD19137E2179);
        break;
    default:
        return -1;
    }
    ctx->count = 0;
    return 0;
}

void av_sha512_update(AVSHA512* ctx, const uint8_t* data, unsigned int len)
{
    unsigned int i, j;

    j = ctx->count & 127;
    ctx->count += len;
#if CONFIG_SMALL
    for (i = 0; i < len; i++) {
        ctx->buffer[j++] = data[i];
        if (128 == j) {
            sha512_transform(ctx->state, ctx->buffer);
            j = 0;
        }
    }
#else
    if ((j + len) > 127) {
        memcpy(&ctx->buffer[j], data, (i = 128 - j));
        sha512_transform(ctx->state, ctx->buffer);
        for (; i + 127 < len; i += 128)
            sha512_transform(ctx->state, &data[i]);
        j = 0;
    } else
        i = 0;
    memcpy(&ctx->buffer[j], &data[i], len - i);
#endif
}

void av_sha512_final(AVSHA512* ctx, uint8_t *digest)
{
    uint64_t i = 0;
    uint64_t finalcount = av_be2ne64(ctx->count << 3);

    av_sha512_update(ctx, "\200", 1);
    while ((ctx->count & 127) != 112)
        av_sha512_update(ctx, "", 1);
    av_sha512_update(ctx, (uint8_t *)&i, 8);
    av_sha512_update(ctx, (uint8_t *)&finalcount, 8); /* Should cause a transform() */
    for (i = 0; i < ctx->digest_len; i++)
        AV_WB64(digest + i*8, ctx->state[i]);
    if (ctx->digest_len & 1) /* SHA512/224 is 28 bytes, and is not divisible by 8. */
        AV_WB32(digest + i*8, ctx->state[i] >> 32);
}

#ifdef TEST
#include <stdio.h>

int main(void)
{
    int i, j, k;
    AVSHA512 ctx;
    unsigned char digest[64];
    const int lengths[4] = { 224, 256, 384, 512 };

    for (j = 0; j < 4; j++) {
        if (j < 2) printf("Testing SHA-512/%d\n", lengths[j]);
        else       printf("Testing SHA-%d\n", lengths[j]);
        for (k = 0; k < 3; k++) {
            av_sha512_init(&ctx, lengths[j]);
            if (k == 0)
                av_sha512_update(&ctx, "abc", 3);
            else if (k == 1)
                av_sha512_update(&ctx, "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                                       "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", 112);
            else
                for (i = 0; i < 1000*1000; i++)
                    av_sha512_update(&ctx, "a", 1);
            av_sha512_final(&ctx, digest);
            for (i = 0; i < lengths[j] >> 3; i++)
                printf("%02X", digest[i]);
            putchar('\n');
        }
        switch (j) { //test vectors (from FIPS PUB 180-4 Apendix A)
        case 0:
            printf("4634270f 707b6a54 daae7530 460842e2 0e37ed26 5ceee9a4 3e8924aa\n"
                   "23fec5bb 94d60b23 30819264 0b0c4533 35d66473 4fe40e72 68674af9\n"
                   "37ab331d 76f0d36d e422bd0e deb22a28 accd487b 7a8453ae 965dd287\n");
            break;
        case 1:
            printf("53048e26 81941ef9 9b2e29b7 6b4c7dab e4c2d0c6 34fc6d46 e0e2f131 07e7af23\n"
                   "3928e184 fb8690f8 40da3988 121d31be 65cb9d3e f83ee614 6feac861 e19b563a\n"
                   "9a59a052 930187a9 7038cae6 92f30708 aa649192 3ef51943 94dc68d5 6c74fb21\n");
            break;
        case 2:
            printf("cb00753f 45a35e8b b5a03d69 9ac65007 272c32ab 0eded163 "
                   "1a8b605a 43ff5bed 8086072b a1e7cc23 58baeca1 34c825a7\n"
                   "09330c33 f71147e8 3d192fc7 82cd1b47 53111b17 3b3b05d2 "
                   "2fa08086 e3b0f712 fcc7c71a 557e2db9 66c3e9fa 91746039\n"
                   "9d0e1809 716474cb 086e834e 310a4a1c ed149e9c 00f24852 "
                   "7972cec5 704c2a5b 07b8b3dc 38ecc4eb ae97ddd8 7f3d8985\n");
            break;
        case 3:
            printf("ddaf35a1 93617aba cc417349 ae204131 12e6fa4e 89a97ea2 0a9eeee6 4b55d39a "
                   "2192992a 274fc1a8 36ba3c23 a3feebbd 454d4423 643ce80e 2a9ac94f a54ca49f\n"
                   "8e959b75 dae313da 8cf4f728 14fc143f 8f7779c6 eb9f7fa1 7299aead b6889018 "
                   "501d289e 4900f7e4 331b99de c4b5433a c7d329ee b6dd2654 5e96e55b 874be909\n"
                   "e718483d 0ce76964 4e2e42c7 bc15b463 8e1f98b1 3b204428 5632a803 afa973eb "
                   "de0ff244 877ea60a 4cb0432c e577c31b eb009c5c 2c49aa2e 4eadb217 ad8cc09b\n");
            break;
        }
    }

    return 0;
}
#endif

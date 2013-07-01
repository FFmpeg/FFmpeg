/*
 * Copyright (C) 2013 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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
#include <stdint.h>
#include "mem.h"
#include "intreadwrite.h"
#include "murmur3.h"

typedef struct AVMurMur3 {
    uint64_t h1, h2;
    uint8_t state[16];
    int state_pos;
    uint64_t len;
} AVMurMur3;

AVMurMur3 *av_murmur3_alloc(void)
{
    return av_mallocz(sizeof(AVMurMur3));
}

void av_murmur3_init_seeded(AVMurMur3 *c, uint64_t seed)
{
    memset(c, 0, sizeof(*c));
    c->h1 = c->h2 = seed;
}

void av_murmur3_init(AVMurMur3 *c)
{
    // arbitrary random number as seed
    av_murmur3_init_seeded(c, 0x725acc55daddca55);
}

static const uint64_t c1 = UINT64_C(0x87c37b91114253d5);
static const uint64_t c2 = UINT64_C(0x4cf5ad432745937f);

#define ROT(a, b) ((a << b) | (a >> (64 - b)))

static uint64_t inline get_k1(const uint8_t *src)
{
    uint64_t k = AV_RL64(src);
    k *= c1;
    k = ROT(k, 31);
    k *= c2;
    return k;
}

static uint64_t inline get_k2(const uint8_t *src)
{
    uint64_t k = AV_RL64(src + 8);
    k *= c2;
    k = ROT(k, 33);
    k *= c1;
    return k;
}

static uint64_t inline update_h1(uint64_t k, uint64_t h1, uint64_t h2)
{
    k ^= h1;
    k = ROT(k, 27);
    k += h2;
    k *= 5;
    k += 0x52dce729;
    return k;
}

static uint64_t inline update_h2(uint64_t k, uint64_t h1, uint64_t h2)
{
    k ^= h2;
    k = ROT(k, 31);
    k += h1;
    k *= 5;
    k += 0x38495ab5;
    return k;
}

void av_murmur3_update(AVMurMur3 *c, const uint8_t *src, int len)
{
    const uint8_t *end;
    uint64_t h1 = c->h1, h2 = c->h2;
    uint64_t k1, k2;
    if (len <= 0) return;
    c->len += len;
    if (c->state_pos > 0) {
        while (c->state_pos < 16) {
            c->state[c->state_pos++] = *src++;
            if (--len <= 0) return;
        }
        c->state_pos = 0;
        k1 = get_k1(c->state);
        k2 = get_k2(c->state);
        h1 = update_h1(k1, h1, h2);
        h2 = update_h2(k2, h1, h2);
    }

    end = src + (len & ~15);
    while (src < end) {
        // These could be done sequentially instead
        // of interleaved, but like this is over 10% faster
        k1 = get_k1(src);
        k2 = get_k2(src);
        h1 = update_h1(k1, h1, h2);
        h2 = update_h2(k2, h1, h2);
        src += 16;
    }
    c->h1 = h1;
    c->h2 = h2;

    len &= 15;
    if (len > 0) {
        memcpy(c->state, src, len);
        c->state_pos = len;
    }
}

static inline uint64_t fmix(uint64_t k)
{
    k ^= k >> 33;
    k *= UINT64_C(0xff51afd7ed558ccd);
    k ^= k >> 33;
    k *= UINT64_C(0xc4ceb9fe1a85ec53);
    k ^= k >> 33;
    return k;
}

void av_murmur3_final(AVMurMur3 *c, uint8_t dst[16])
{
    uint64_t h1 = c->h1, h2 = c->h2;
    memset(c->state + c->state_pos, 0, sizeof(c->state) - c->state_pos);
    h1 ^= get_k1(c->state) ^ c->len;
    h2 ^= get_k2(c->state) ^ c->len;
    h1 += h2;
    h2 += h1;
    h1 = fmix(h1);
    h2 = fmix(h2);
    h1 += h2;
    h2 += h1;
    AV_WL64(dst, h1);
    AV_WL64(dst + 8, h2);
}

#ifdef TEST
int main(void)
{
    int i;
    uint8_t hash_result[16] = {0};
    AVMurMur3 *ctx = av_murmur3_alloc();
#if 1
    uint8_t in[256] = {0};
    uint8_t *hashes = av_mallocz(256 * 16);
    for (i = 0; i < 256; i++)
    {
        in[i] = i;
        av_murmur3_init_seeded(ctx, 256 - i);
        // Note: this actually tests hashing 0 bytes
        av_murmur3_update(ctx, in, i);
        av_murmur3_final(ctx, hashes + 16 * i);
    }
    av_murmur3_init_seeded(ctx, 0);
    av_murmur3_update(ctx, hashes, 256 * 16);
    av_murmur3_final(ctx, hash_result);
    av_free(hashes);
    av_freep(&ctx);
    printf("result: 0x%"PRIx64" 0x%"PRIx64"\n", AV_RL64(hash_result), AV_RL64(hash_result + 8));
    // official reference value is 32 bit
    return AV_RL32(hash_result) != 0x6384ba69;
#else
    uint8_t *in = av_mallocz(512*1024);
    av_murmur3_init(ctx);
    for (i = 0; i < 40*1024; i++)
        av_murmur3_update(ctx, in, 512*1024);
    av_murmur3_final(ctx, hash_result);
    av_free(in);
    return hash_result[0];
#endif
}
#endif

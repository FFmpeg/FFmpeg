/*
 * RC4 encryption/decryption/pseudo-random number generator
 * Copyright (c) 2007 Reimar Doeffinger
 *
 * loosely based on LibTomCrypt by Tom St Denis
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
#include "mem.h"
#include "rc4.h"

AVRC4 *av_rc4_alloc(void)
{
    return av_mallocz(sizeof(struct AVRC4));
}

int av_rc4_init(AVRC4 *r, const uint8_t *key, int key_bits, int decrypt) {
    int i, j;
    uint8_t y;
    uint8_t *state = r->state;
    int keylen = key_bits >> 3;
    if (key_bits & 7)
        return AVERROR(EINVAL);
    for (i = 0; i < 256; i++)
        state[i] = i;
    y = 0;
    // j is i % keylen
    for (j = 0, i = 0; i < 256; i++, j++) {
        if (j == keylen) j = 0;
        y += state[i] + key[j];
        FFSWAP(uint8_t, state[i], state[y]);
    }
    r->x = 1;
    r->y = state[1];
    return 0;
}

void av_rc4_crypt(AVRC4 *r, uint8_t *dst, const uint8_t *src, int count, uint8_t *iv, int decrypt) {
    uint8_t x = r->x, y = r->y;
    uint8_t *state = r->state;
    while (count-- > 0) {
        uint8_t sum = state[x] + state[y];
        FFSWAP(uint8_t, state[x], state[y]);
        *dst++ = src ? *src++ ^ state[sum] : state[sum];
        x++;
        y += state[x];
    }
    r->x = x; r->y = y;
}

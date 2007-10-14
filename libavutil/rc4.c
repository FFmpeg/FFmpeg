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
#include "common.h"
#include "rc4.h"

void ff_rc4_enc(const uint8_t *key, int keylen, uint8_t *data, int datalen) {
    int i, j;
    uint8_t x, y;
    uint8_t state[256];
    for (i = 0; i < 256; i++)
        state[i] = i;
    y = 0;
    // j is i % keylen
    for (j = 0, i = 0; i < 256; i++, j++) {
        if (j == keylen) j = 0;
        y += state[i] + key[j];
        FFSWAP(uint8_t, state[i], state[y]);
    }
    // state initialized, now do the real encryption
    x = 1; y = state[1];
    while (datalen-- > 0) {
        uint8_t sum = state[x] + state[y];
        FFSWAP(uint8_t, state[x], state[y]);
        *data++ ^= state[sum];
        x++;
        y += state[x];
    }
}

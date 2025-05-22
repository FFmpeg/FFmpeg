/*
 * AAC definitions and structures
 * Copyright (c) 2024 Lynne
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

#include "libavcodec/aactab.h"
#include "aacdec_ac.h"

uint32_t ff_aac_ac_map_process(AACArithState *state, int reset, int N)
{
    float ratio;
    if (reset) {
        memset(state->last, 0, sizeof(state->last));
        state->last_len = N;
    } else if (state->last_len != N) {
        int i;
        uint8_t last[512 /* 2048 / 4 */];
        memcpy(last, state->last, sizeof(last));

        ratio = state->last_len / (float)N;
        for (i = 0; i < N/2; i++) {
            int k = (int)(i * ratio);
            state->last[i] = last[k];
        }

        for (; i < FF_ARRAY_ELEMS(state->last); i++)
            state->last[i] = 0;

        state->last_len = N;
    }

    state->cur[3] = 0;
    state->cur[2] = 0;
    state->cur[1] = 0;
    state->cur[0] = 1;

    state->state_pre = state->last[0] << 12;
    return state->last[0] << 12;
}

uint32_t ff_aac_ac_get_context(AACArithState *state, uint32_t c, int i, int N)
{
    c = state->state_pre >> 8;
    c = c + (state->last[i + 1] << 8);
    c = (c << 4);
    c += state->cur[1];

    state->state_pre = c;

    if (i > 3 &&
        ((state->cur[3] + state->cur[2] + state->cur[1]) < 5))
        return c + 0x10000;

    return c;
}

uint32_t ff_aac_ac_get_pk(uint32_t c)
{
    int i_min = -1;
    int i, j;
    int i_max = FF_ARRAY_ELEMS(ff_aac_ac_lookup_m) - 1;
    while ((i_max - i_min) > 1) {
        i = i_min + ((i_max - i_min) / 2);
        j = ff_aac_ac_hash_m[i];
        if (c < (j >> 8))
            i_max = i;
        else if (c > (j >> 8))
            i_min = i;
        else
            return (j & 0xFF);
    }
    return ff_aac_ac_lookup_m[i_max];
}

void ff_aac_ac_update_context(AACArithState *state, int idx,
                              uint16_t a, uint16_t b)
{
    state->cur[0] = FFMIN(a + b + 1, 0xF);
    state->cur[3] = state->cur[2];
    state->cur[2] = state->cur[1];
    state->cur[1] = state->cur[0];

    state->last[idx] = state->cur[0];
}

/* Initialize AC */
void ff_aac_ac_init(AACArith *ac, GetBitContext *gb)
{
    ac->low = 0;
    ac->high = UINT16_MAX;
    ac->val = get_bits(gb, 16);
}

uint16_t ff_aac_ac_decode(AACArith *ac, GetBitContext *gb,
                          const uint16_t *cdf, uint16_t cdf_len)
{
    int val = ac->val;
    int low = ac->low;
    int high = ac->high;

    int sym;
    int rng = high - low + 1;
    int c = ((((int)(val - low + 1)) << 14) - ((int)1));

    const uint16_t *p = cdf - 1;

    /* One for each possible CDF length in the spec */
    switch (cdf_len) {
    case 2:
        if ((p[1] * rng) > c)
            p += 1;
        break;
    case 4:
        if ((p[2] * rng) > c)
            p += 2;
        if ((p[1] * rng) > c)
            p += 1;
        break;
    case 17:
        /* First check if the current probability is even met at all */
        if ((p[1] * rng) <= c)
            break;
        p += 1;
        for (int i = 8; i >= 1; i >>= 1)
            if ((p[i] * rng) > c)
                p += i;
        break;
    case 27:
        if ((p[16] * rng) > c)
            p += 16;
        if ((p[8] * rng) > c)
            p += 8;
        if (p != (cdf - 1 + 24))
            if ((p[4] * rng) > c)
                p += 4;
        if ((p[2] * rng) > c)
            p += 2;

        if (p != (cdf - 1 + 24 + 2))
            if ((p[1] * rng) > c)
                p += 1;
        break;
    default:
        /* This should never happen */
        av_assert2(0);
    }

    sym = (int)((ptrdiff_t)(p - cdf)) + 1;
    if (sym)
        high = low + ((rng * cdf[sym - 1]) >> 14) - 1;
    low += (rng * cdf[sym]) >> 14;

    /* This loop could be done faster */
    while (1) {
        if (high < 32768) {
            ;
        } else if (low >= 32768) {
            val -= 32768;
            low -= 32768;
            high -= 32768;
        } else if (low >= 16384 && high < 49152) {
            val -= 16384;
            low -= 16384;
            high -= 16384;
        } else {
            break;
        }
        low += low;
        high += high + 1;
        val = (val << 1) | get_bits1(gb);
    };

    ac->low = low;
    ac->high = high;
    ac->val = val;

    return sym;
}

void ff_aac_ac_finish(AACArithState *state, int offset, int N)
{
    int i;

    for (i = offset; i < N/2; i++)
        state->last[i] = 1;

    for (; i < FF_ARRAY_ELEMS(state->last); i++)
        state->last[i] = 0;
}

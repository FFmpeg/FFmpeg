/*
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

/*
 * Check the C inverse transform against the matrix product of
 * GB/T 20090.2-2013 9.8.2 and GY/T 257.1-2012 9.7.
 * The SIMD versions are checked against the C version by checkasm.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/common.h"
#include "libavutil/cpu.h"
#include "libavutil/lfg.h"
#include "libavutil/mem_internal.h"

#include "libavcodec/cavsdsp.h"
#include "libavcodec/idctdsp.h"

#define STRIDE 16

static const int8_t t8[8][8] = {
    { 8,  10,  10,   9,  8,   6,   4,   2 },
    { 8,   9,   4,  -2, -8, -10, -10,  -6 },
    { 8,   6,  -4, -10, -8,   2,  10,   9 },
    { 8,   2, -10,  -6,  8,   9,  -4, -10 },
    { 8,  -2, -10,   6,  8,  -9,  -4,  10 },
    { 8,  -6,  -4,  10, -8,  -2,  10,  -9 },
    { 8,  -9,   4,   2, -8,  10, -10,   6 },
    { 8, -10,  10,  -9,  8,  -6,   4,  -2 },
};

static void idct8_add_ref(uint8_t *dst, const int16_t coeffs[64])
{
    int tmp[8][8];

    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            int sum = 4;
            for (int k = 0; k < 8; k++)
                sum += coeffs[8 * i + k] * t8[j][k];
            tmp[i][j] = av_clip_int16(sum) >> 3;
        }
    }
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            int sum = 64;
            for (int k = 0; k < 8; k++)
                sum += t8[i][k] * tmp[k][j];
            dst[j] = av_clip_uint8(dst[j] + (av_clip_int16(sum) >> 7));
        }
        dst += STRIDE;
    }
}

static int check_block(CAVSDSPContext *c, const uint8_t *perm,
                       const int16_t coeffs[64], AVLFG *lfg, int n)
{
    DECLARE_ALIGNED(16, int16_t, block)[64];
    DECLARE_ALIGNED(16, uint8_t, ref)[8 * STRIDE];
    DECLARE_ALIGNED(16, uint8_t, out)[8 * STRIDE];

    for (int i = 0; i < 64; i++)
        block[perm[i]] = coeffs[i];
    for (int i = 0; i < 8 * STRIDE; i++)
        ref[i] = out[i] = av_lfg_get(lfg);

    idct8_add_ref(ref, coeffs);
    c->cavs_idct8_add(out, block, STRIDE);
    if (memcmp(ref, out, sizeof(ref))) {
        fprintf(stderr, "cavs_idct8_add: mismatch in block %d\n", n);
        return 1;
    }
    return 0;
}

int main(void)
{
    CAVSDSPContext c;
    uint8_t perm[64];
    int16_t coeffs[64];
    AVLFG lfg;
    int n = 0, ret = 0;

    av_force_cpu_flags(0);
    ff_cavsdsp_init(&c);
    ff_init_scantable_permutation(perm, c.idct_perm);
    av_lfg_init(&lfg, 1);

    /* drive each first pass sum to both ends of its range */
    for (int j = 0; j < 8; j++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            for (int i = 0; i < 64; i++)
                coeffs[i] = (t8[j][i & 7] * sign < 0) ? INT16_MIN : INT16_MAX;
            ret |= check_block(&c, perm, coeffs, &lfg, n++);
        }
    }

    /* magnitudes from the whole int16_t range down to single digits */
    for (int iter = 0; iter < 100000; iter++) {
        int count = 1 + av_lfg_get(&lfg) % 64;

        memset(coeffs, 0, sizeof(coeffs));
        for (int k = 0; k < count; k++) {
            unsigned r = av_lfg_get(&lfg);
            coeffs[r & 63] = (int16_t)av_lfg_get(&lfg) >> (r >> 6 & 15);
        }
        ret |= check_block(&c, perm, coeffs, &lfg, n++);
    }

    return ret;
}

/*
 * Header file for hardcoded AAC cube-root table
 *
 * Copyright (c) 2010 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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

#ifndef AVCODEC_CBRT_TABLEGEN_H
#define AVCODEC_CBRT_TABLEGEN_H

#include <stdint.h>
#include <math.h>
#include "libavutil/attributes.h"
#include "libavutil/intfloat.h"
#include "libavcodec/aac_defines.h"

#if USE_FIXED
#define CBRT(x) lrint((x) * 8192)
#else
#define CBRT(x) av_float2int((float)(x))
#endif

#define LUT_SIZE     (1 << 13)
#define TMP_LUT_SIZE (LUT_SIZE/2)

uint32_t AAC_RENAME(ff_cbrt_tab)[LUT_SIZE];

av_cold void AAC_RENAME(ff_cbrt_tableinit)(void)
{
    // LUT of k^{4/3} for odd k; element idx corresponds to 2 * idx + 1.
    static double cbrt_tab_dbl[TMP_LUT_SIZE];

    for (int idx = 0; idx < TMP_LUT_SIZE; ++idx)
        cbrt_tab_dbl[idx] = 1;

    /* have to take care of non-squarefree numbers; notice that sqrt(LUT_SIZE) = 90;
     * idx == 44 corresponds to 89. */
    for (int idx = 1; idx < 45; ++idx) {
        if (cbrt_tab_dbl[idx] == 1) {
            int i = 2 * idx + 1;
            double cbrt_val = i * cbrt(i);
            for (int k = i; k < LUT_SIZE; k *= i) {
                // We only have to handle k, 3 * k, 5 * k,...,
                // because only these are odd. The corresponding indices are
                // k >> 1, (k >> 1) + k, (k >> 1) + 2 * k,...
                for (int idx2 = k >> 1; idx2 < TMP_LUT_SIZE; idx2 += k)
                    cbrt_tab_dbl[idx2] *= cbrt_val;
            }
        }
    }

    for (int idx = 45; idx < TMP_LUT_SIZE; ++idx) {
        if (cbrt_tab_dbl[idx] == 1) {
            int i = 2 * idx + 1;
            double cbrt_val = i * cbrt(i);
            for (int idx2 = idx; idx2 < TMP_LUT_SIZE; idx2 += i)
                cbrt_tab_dbl[idx2] *= cbrt_val;
        }
    }

    double cbrt_2 = 2 * cbrt(2);
    for (int idx = 0; idx < TMP_LUT_SIZE; ++idx) {
        double cbrt_val = cbrt_tab_dbl[idx];
        for (int i = 2 * idx + 1; i < LUT_SIZE; i *= 2) {
            AAC_RENAME(ff_cbrt_tab)[i] = CBRT(cbrt_val);
            cbrt_val *= cbrt_2;
        }
    }
    AAC_RENAME(ff_cbrt_tab)[0] = CBRT(0);
}

#endif /* AVCODEC_CBRT_TABLEGEN_H */

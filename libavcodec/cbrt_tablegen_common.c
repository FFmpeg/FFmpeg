/*
 * Common code for AAC cube-root table
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

#include <math.h>

#include "cbrt_data.h"
#include "libavutil/attributes.h"
#ifdef HAVE_AV_CONFIG_H // Only include libm.h when building for the target, not the host
#include "libavutil/libm.h"
#endif

av_cold void ff_cbrt_dbl_tableinit(double tmp_lut[TMP_LUT_SIZE])
{
    for (int idx = 0; idx < TMP_LUT_SIZE; ++idx)
        tmp_lut[idx] = 1;

    /* have to take care of non-squarefree numbers; notice that sqrt(LUT_SIZE) = 90;
     * idx == 44 corresponds to 89. */
    for (int idx = 1; idx < 45; ++idx) {
        if (tmp_lut[idx] == 1) {
            int i = 2 * idx + 1;
            double cbrt_val = i * cbrt(i);
            for (int k = i; k < LUT_SIZE; k *= i) {
                // We only have to handle k, 3 * k, 5 * k,...,
                // because only these are odd. The corresponding indices are
                // k >> 1, (k >> 1) + k, (k >> 1) + 2 * k,...
                for (int idx2 = k >> 1; idx2 < TMP_LUT_SIZE; idx2 += k)
                    tmp_lut[idx2] *= cbrt_val;
            }
        }
    }

    for (int idx = 45; idx < TMP_LUT_SIZE; ++idx) {
        if (tmp_lut[idx] == 1) {
            int i = 2 * idx + 1;
            double cbrt_val = i * cbrt(i);
            for (int idx2 = idx; idx2 < TMP_LUT_SIZE; idx2 += i)
                tmp_lut[idx2] *= cbrt_val;
        }
    }
}

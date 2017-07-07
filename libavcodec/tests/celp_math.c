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

#include "libavutil/libm.h"
#include "libavcodec/celp_math.c"

static inline void IsAlmostEqual(float A, float B, float epsilon)
{
    float diff = fabsf(A - B);
    float absa = fabsf(A);
    float absb = fabsf(B);
    float largest = (absb > absa) ? absb : absa;
    av_assert0(diff <= largest * epsilon);
}

int main(void)
{
    int i;
    const float f1[3]   = {0.0,  1.1,  2.2};
    const float f2[3]   = {3.3,  4.4,  5.5};
    const int16_t i1[3] = {6,  7,  8};
    const int16_t i2[3] = {9, 10, 11};

    float   r = ff_dot_productf(f1, f2, FF_ARRAY_ELEMS(f1));
    int64_t d = ff_dot_product(i1, i2, FF_ARRAY_ELEMS(i1));

    IsAlmostEqual(16.94f, r, 0.000001f);
    av_assert0(212 == d);

    for (i = 1024; i >= 1; i/=2)
        av_assert0(ff_log2_q15(i) == (1<<15)*((int)log2(i))+(1<<2));

    return 0;
}

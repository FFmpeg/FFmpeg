/*
 * various filters for ACELP-based codecs
 *
 * Copyright (c) 2008 Vladimir Voroshilov
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <inttypes.h>

#include "avcodec.h"
#include "celp_filters.h"
#include "libavutil/common.h"

void ff_celp_convolve_circ(int16_t* fc_out, const int16_t* fc_in,
                           const int16_t* filter, int len)
{
    int i, k;

    memset(fc_out, 0, len * sizeof(int16_t));

    /* Since there are few pulses over an entire subframe (i.e. almost
       all fc_in[i] are zero) it is faster to loop over fc_in first. */
    for (i = 0; i < len; i++) {
        if (fc_in[i]) {
            for (k = 0; k < i; k++)
                fc_out[k] += (fc_in[i] * filter[len + k - i]) >> 15;

            for (k = i; k < len; k++)
                fc_out[k] += (fc_in[i] * filter[      k - i]) >> 15;
        }
    }
}

void ff_celp_circ_addf(float *out, const float *in,
                       const float *lagged, int lag, float fac, int n)
{
    int k;
    for (k = 0; k < lag; k++)
        out[k] = in[k] + fac * lagged[n + k - lag];
    for (; k < n; k++)
        out[k] = in[k] + fac * lagged[    k - lag];
}

int ff_celp_lp_synthesis_filter(int16_t *out, const int16_t *filter_coeffs,
                                const int16_t *in, int buffer_length,
                                int filter_length, int stop_on_overflow,
                                int shift, int rounder)
{
    int i,n;

    for (n = 0; n < buffer_length; n++) {
        int sum = -rounder, sum1;
        for (i = 1; i <= filter_length; i++)
            sum += filter_coeffs[i-1] * out[n-i];

        sum1 = ((-sum >> 12) + in[n]) >> shift;
        sum  = av_clip_int16(sum1);

        if (stop_on_overflow && sum != sum1)
            return 1;

        out[n] = sum;
    }

    return 0;
}

void ff_celp_lp_synthesis_filterf(float *out, const float *filter_coeffs,
                                  const float* in, int buffer_length,
                                  int filter_length)
{
    int i,n;

#if 0 // Unoptimized code path for improved readability
    for (n = 0; n < buffer_length; n++) {
        out[n] = in[n];
        for (i = 1; i <= filter_length; i++)
            out[n] -= filter_coeffs[i-1] * out[n-i];
    }
#else
    float out0, out1, out2, out3;
    float old_out0, old_out1, old_out2, old_out3;
    float a,b,c;

    a = filter_coeffs[0];
    b = filter_coeffs[1];
    c = filter_coeffs[2];
    b -= filter_coeffs[0] * filter_coeffs[0];
    c -= filter_coeffs[1] * filter_coeffs[0];
    c -= filter_coeffs[0] * b;

    old_out0 = out[-4];
    old_out1 = out[-3];
    old_out2 = out[-2];
    old_out3 = out[-1];
    for (n = 0; n <= buffer_length - 4; n+=4) {
        float tmp0,tmp1,tmp2;
        float val;

        out0 = in[0];
        out1 = in[1];
        out2 = in[2];
        out3 = in[3];

        out0 -= filter_coeffs[2] * old_out1;
        out1 -= filter_coeffs[2] * old_out2;
        out2 -= filter_coeffs[2] * old_out3;

        out0 -= filter_coeffs[1] * old_out2;
        out1 -= filter_coeffs[1] * old_out3;

        out0 -= filter_coeffs[0] * old_out3;

        val = filter_coeffs[3];

        out0 -= val * old_out0;
        out1 -= val * old_out1;
        out2 -= val * old_out2;
        out3 -= val * old_out3;

        for (i = 5; i <= filter_length; i += 2) {
            old_out3 = out[-i];
            val = filter_coeffs[i-1];

            out0 -= val * old_out3;
            out1 -= val * old_out0;
            out2 -= val * old_out1;
            out3 -= val * old_out2;

            old_out2 = out[-i-1];

            val = filter_coeffs[i];

            out0 -= val * old_out2;
            out1 -= val * old_out3;
            out2 -= val * old_out0;
            out3 -= val * old_out1;

            FFSWAP(float, old_out0, old_out2);
            old_out1 = old_out3;
        }

        tmp0 = out0;
        tmp1 = out1;
        tmp2 = out2;

        out3 -= a * tmp2;
        out2 -= a * tmp1;
        out1 -= a * tmp0;

        out3 -= b * tmp1;
        out2 -= b * tmp0;

        out3 -= c * tmp0;


        out[0] = out0;
        out[1] = out1;
        out[2] = out2;
        out[3] = out3;

        old_out0 = out0;
        old_out1 = out1;
        old_out2 = out2;
        old_out3 = out3;

        out += 4;
        in  += 4;
    }

    out -= n;
    in -= n;
    for (; n < buffer_length; n++) {
        out[n] = in[n];
        for (i = 1; i <= filter_length; i++)
            out[n] -= filter_coeffs[i-1] * out[n-i];
    }
#endif
}

void ff_celp_lp_zero_synthesis_filterf(float *out, const float *filter_coeffs,
                                       const float *in, int buffer_length,
                                       int filter_length)
{
    int i,n;

    for (n = 0; n < buffer_length; n++) {
        out[n] = in[n];
        for (i = 1; i <= filter_length; i++)
            out[n] += filter_coeffs[i-1] * in[n-i];
    }
}

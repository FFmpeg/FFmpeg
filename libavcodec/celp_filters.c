/*
 * various filters for ACELP-based codecs
 *
 * Copyright (c) 2008 Vladimir Voroshilov
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

#include <inttypes.h>

#include "avcodec.h"
#include "celp_filters.h"

void ff_celp_convolve_circ(
        int16_t* fc_out,
        const int16_t* fc_in,
        const int16_t* filter,
        int len)
{
    int i, k;

    memset(fc_out, 0, len * sizeof(int16_t));

    /* Since there are few pulses over an entire subframe (i.e. almost
       all fc_in[i] are zero) it is faster to loop over fc_in first. */
    for(i=0; i<len; i++)
    {
        if(fc_in[i])
        {
            for(k=0; k<i; k++)
                fc_out[k] += (fc_in[i] * filter[len + k - i]) >> 15;

            for(k=i; k<len; k++)
                fc_out[k] += (fc_in[i] * filter[      k - i]) >> 15;
        }
    }
}

int ff_celp_lp_synthesis_filter(
        int16_t *out,
        const int16_t* filter_coeffs,
        const int16_t* in,
        int buffer_length,
        int filter_length,
        int stop_on_overflow,
        int rounder)
{
    int i,n;

    // These two lines are to avoid a -1 subtraction in the main loop
    filter_length++;
    filter_coeffs--;

    for(n=0; n<buffer_length; n++)
    {
        int sum = rounder;
        for(i=1; i<filter_length; i++)
            sum -= filter_coeffs[i] * out[n-i];

        sum = (sum >> 12) + in[n];

        if(sum + 0x8000 > 0xFFFFU)
        {
            if(stop_on_overflow)
                return 1;
            sum = (sum >> 31) ^ 32767;
        }
        out[n] = sum;
    }

    return 0;
}

void ff_celp_lp_synthesis_filterf(
        float *out,
        const float* filter_coeffs,
        const float* in,
        int buffer_length,
        int filter_length)
{
    int i,n;

    // These two lines are to avoid a -1 subtraction in the main loop
    filter_length++;
    filter_coeffs--;

    for(n=0; n<buffer_length; n++)
    {
        out[n] = in[n];
        for(i=1; i<filter_length; i++)
            out[n] -= filter_coeffs[i] * out[n-i];
    }
}

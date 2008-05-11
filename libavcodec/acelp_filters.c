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
#include "acelp_filters.h"
#define FRAC_BITS 13
#include "mathops.h"

void ff_acelp_convolve_circ(
        int16_t* fc_out,
        const int16_t* fc_in,
        const int16_t* filter,
        int subframe_size)
{
    int i, k;

    memset(fc_out, 0, subframe_size * sizeof(int16_t));

    /* Since there are few pulses over entire subframe (i.e. almost all
       fc_in[i] are zero, in case of G.729D the buffer contains two non-zero
       samples before the call to ff_acelp_enhance_harmonics, and (due to
       pitch_delay bounded to [20; 143]) a maximum four non-zero samples
       for a total of 40 after the call to it), it is faster to swap two loops
       and process non-zero samples only. This will reduce the number of
       multiplications from 40*40 to 4*40 for G.729D */
    for(i=0; i<subframe_size; i++)
    {
        if(fc_in[i])
        {
            for(k=0; k<i; k++)
                fc_out[k] += (fc_in[i] * filter[subframe_size + k - i]) >> 15;

            for(k=i; k<subframe_size; k++)
                fc_out[k] += (fc_in[i] * filter[k - i]) >> 15;
        }
    }
}

int ff_acelp_lp_synthesis_filter(
        int16_t *out,
        const int16_t* filter_coeffs,
        const int16_t* in,
        int buffer_length,
        int filter_length,
        int stop_on_overflow)
{
    int i,n;

    for(n=0; n<buffer_length; n++)
    {
        int sum = 0x800;
        for(i=1; i<filter_length; i++)
            sum -= filter_coeffs[i] * out[n-i];

        sum = (sum >> 12) + in[n];

        /* Check for overflow */
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

void ff_acelp_weighted_filter(
        int16_t *out,
        const int16_t* in,
        const int16_t *weight_pow,
        int filter_length)
{
    int n;
    for(n=0; n<filter_length; n++)
        out[n] = (in[n] * weight_pow[n] + 0x4000) >> 15; /* (3.12) = (0.15) * (3.12) with rounding */
}

void ff_acelp_high_pass_filter(
        int16_t* out,
        int hpf_f[2],
        const int16_t* in,
        int length)
{
    int i;
    int tmp;

    for(i=0; i<length; i++)
    {
        tmp =  MULL(hpf_f[0], 15836);                     /* (14.13) = (13.13) * (1.13) */
        tmp += MULL(hpf_f[1], -7667);                     /* (13.13) = (13.13) * (0.13) */
        tmp += 7699 * (in[i] - 2*in[i-1] + in[i-2]); /* (14.13) =  (0.13) * (14.0) */

        /* Multiplication by 2 with rounding can cause short type
           overflow, thus clipping is required. */

        out[i] = av_clip_int16((tmp + 0x800) >> 12);      /* (15.0) = 2 * (13.13) = (14.13) */

        hpf_f[1] = hpf_f[0];
        hpf_f[0] = tmp;
    }
}

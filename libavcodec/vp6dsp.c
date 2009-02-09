/**
 * @file libavcodec/vp6dsp.c
 * VP6 DSP-oriented functions
 *
 * Copyright (C) 2006  Aurelien Jacobs <aurel@gnuage.org>
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

#include "libavutil/common.h"
#include "dsputil.h"


void ff_vp6_filter_diag4_c(uint8_t *dst, uint8_t *src, int stride,
                           const int16_t *h_weights, const int16_t *v_weights)
{
    int x, y;
    int tmp[8*11];
    int *t = tmp;

    src -= stride;

    for (y=0; y<11; y++) {
        for (x=0; x<8; x++) {
            t[x] = av_clip_uint8((  src[x-1] * h_weights[0]
                               + src[x  ] * h_weights[1]
                               + src[x+1] * h_weights[2]
                               + src[x+2] * h_weights[3] + 64) >> 7);
        }
        src += stride;
        t += 8;
    }

    t = tmp + 8;
    for (y=0; y<8; y++) {
        for (x=0; x<8; x++) {
            dst[x] = av_clip_uint8((  t[x-8 ] * v_weights[0]
                                 + t[x   ] * v_weights[1]
                                 + t[x+8 ] * v_weights[2]
                                 + t[x+16] * v_weights[3] + 64) >> 7);
        }
        dst += stride;
        t += 8;
    }
}

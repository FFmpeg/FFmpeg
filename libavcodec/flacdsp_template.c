/*
 * Copyright (c) 2012 Mans Rullgard <mans@mansr.com>
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

#include <stdint.h>

#undef FUNC
#undef sample

#if SAMPLE_SIZE == 32
#   define FUNC(n) n ## _32
#   define sample  int32_t
#else
#   define FUNC(n) n ## _16
#   define sample  int16_t
#endif

static void FUNC(flac_decorrelate_indep_c)(uint8_t **out, int32_t **in,
                                           int channels, int len, int shift)
{
    sample *samples = (sample *) out[0];
    int i, j;

    for (j = 0; j < len; j++)
        for (i = 0; i < channels; i++)
            *samples++ = in[i][j] << shift;
}

static void FUNC(flac_decorrelate_ls_c)(uint8_t **out, int32_t **in,
                                        int channels, int len, int shift)
{
    sample *samples = (sample *) out[0];
    int i;

    for (i = 0; i < len; i++) {
        int a = in[0][i];
        int b = in[1][i];
        *samples++ =  a      << shift;
        *samples++ = (a - b) << shift;
    }
}

static void FUNC(flac_decorrelate_rs_c)(uint8_t **out, int32_t **in,
                                        int channels, int len, int shift)
{
    sample *samples = (sample *) out[0];
    int i;

    for (i = 0; i < len; i++) {
        int a = in[0][i];
        int b = in[1][i];
        *samples++ = (a + b) << shift;
        *samples++ =  b      << shift;
    }
}

static void FUNC(flac_decorrelate_ms_c)(uint8_t **out, int32_t **in,
                                        int channels, int len, int shift)
{
    sample *samples = (sample *) out[0];
    int i;

    for (i = 0; i < len; i++) {
        int a = in[0][i];
        int b = in[1][i];
        a -= b >> 1;
        *samples++ = (a + b) << shift;
        *samples++ =  a      << shift;
    }
}

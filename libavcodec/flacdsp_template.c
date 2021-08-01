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
#include "libavutil/macros.h"

#undef FUNC
#undef FSUF
#undef sample
#undef sample_type
#undef OUT
#undef S

#if SAMPLE_SIZE == 32
#   define sample_type  int32_t
#else
#   define sample_type  int16_t
#endif

#if PLANAR
#   define FSUF   AV_JOIN(SAMPLE_SIZE, p)
#   define sample sample_type *
#   define OUT(n) n
#   define S(s, c, i) (s[c][i])
#else
#   define FSUF   SAMPLE_SIZE
#   define sample sample_type
#   define OUT(n) n[0]
#   define S(s, c, i) (*s++)
#endif

#define FUNC(n) AV_JOIN(n ## _, FSUF)

static void FUNC(flac_decorrelate_indep_c)(uint8_t **out, int32_t **in,
                                           int channels, int len, int shift)
{
    sample *samples = (sample *) OUT(out);
    int i, j;

    for (j = 0; j < len; j++)
        for (i = 0; i < channels; i++)
            S(samples, i, j) = (int)((unsigned)in[i][j] << shift);
}

static void FUNC(flac_decorrelate_ls_c)(uint8_t **out, int32_t **in,
                                        int channels, int len, int shift)
{
    sample *samples = (sample *) OUT(out);
    int i;

    for (i = 0; i < len; i++) {
        unsigned a = in[0][i];
        unsigned b = in[1][i];
        S(samples, 0, i) =  a      << shift;
        S(samples, 1, i) = (a - b) << shift;
    }
}

static void FUNC(flac_decorrelate_rs_c)(uint8_t **out, int32_t **in,
                                        int channels, int len, int shift)
{
    sample *samples = (sample *) OUT(out);
    int i;

    for (i = 0; i < len; i++) {
        unsigned a = in[0][i];
        unsigned b = in[1][i];
        S(samples, 0, i) = (a + b) << shift;
        S(samples, 1, i) =  b      << shift;
    }
}

static void FUNC(flac_decorrelate_ms_c)(uint8_t **out, int32_t **in,
                                        int channels, int len, int shift)
{
    sample *samples = (sample *) OUT(out);
    int i;

    for (i = 0; i < len; i++) {
        unsigned a = in[0][i];
        int b = in[1][i];
        a -= b >> 1;
        S(samples, 0, i) = (a + b) << shift;
        S(samples, 1, i) =  a      << shift;
    }
}

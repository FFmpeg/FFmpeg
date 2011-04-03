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

#ifndef AVCODEC_FFT_INTERNAL_H
#define AVCODEC_FFT_INTERNAL_H

#if CONFIG_FFT_FLOAT

#define FIX15(v) (v)
#define sqrthalf (float)M_SQRT1_2

#define BF(x, y, a, b) do {                     \
        x = a - b;                              \
        y = a + b;                              \
    } while (0)

#define CMUL(dre, dim, are, aim, bre, bim) do { \
        (dre) = (are) * (bre) - (aim) * (bim);  \
        (dim) = (are) * (bim) + (aim) * (bre);  \
    } while (0)

#else

#include "libavutil/intmath.h"
#include "mathops.h"

void ff_mdct_calcw_c(FFTContext *s, FFTDouble *output, const FFTSample *input);

#define SCALE_FLOAT(a, bits) lrint((a) * (double)(1 << (bits)))
#define FIX15(a) av_clip(SCALE_FLOAT(a, 15), -32767, 32767)

#define sqrthalf ((int16_t)((1<<15)*M_SQRT1_2))

#define BF(x, y, a, b) do {                     \
        x = (a - b) >> 1;                       \
        y = (a + b) >> 1;                       \
    } while (0)

#define CMULS(dre, dim, are, aim, bre, bim, sh) do {            \
        (dre) = (MUL16(are, bre) - MUL16(aim, bim)) >> sh;      \
        (dim) = (MUL16(are, bim) + MUL16(aim, bre)) >> sh;      \
    } while (0)

#define CMUL(dre, dim, are, aim, bre, bim)      \
    CMULS(dre, dim, are, aim, bre, bim, 15)

#define CMULL(dre, dim, are, aim, bre, bim)     \
    CMULS(dre, dim, are, aim, bre, bim, 0)

#endif /* CONFIG_FFT_FLOAT */

#define ff_imdct_calc_c FFT_NAME(ff_imdct_calc_c)
#define ff_imdct_half_c FFT_NAME(ff_imdct_half_c)
#define ff_mdct_calc_c  FFT_NAME(ff_mdct_calc_c)

void ff_imdct_calc_c(FFTContext *s, FFTSample *output, const FFTSample *input);
void ff_imdct_half_c(FFTContext *s, FFTSample *output, const FFTSample *input);
void ff_mdct_calc_c(FFTContext *s, FFTSample *output, const FFTSample *input);

#endif /* AVCODEC_FFT_INTERNAL_H */

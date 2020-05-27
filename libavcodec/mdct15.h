/*
 * Copyright (c) 2017 Rostislav Pehlivanov <atomnuker@gmail.com>
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

#ifndef AVCODEC_MDCT15_H
#define AVCODEC_MDCT15_H

#include <stddef.h>

#include "libavutil/mem_internal.h"

#include "fft.h"

typedef struct MDCT15Context {
    int fft_n;
    int len2;
    int len4;
    int inverse;
    int *pfa_prereindex;
    int *pfa_postreindex;

    FFTContext ptwo_fft;
    FFTComplex *tmp;
    FFTComplex *twiddle_exptab;

    DECLARE_ALIGNED(32, FFTComplex, exptab)[64];

    /* 15-point FFT */
    void (*fft15)(FFTComplex *out, FFTComplex *in, FFTComplex *exptab, ptrdiff_t stride);

    /* PFA postrotate and exptab */
    void (*postreindex)(FFTComplex *out, FFTComplex *in, FFTComplex *exp, int *lut, ptrdiff_t len8);

    /* Calculate a full 2N -> N MDCT */
    void (*mdct)(struct MDCT15Context *s, float *dst, const float *src, ptrdiff_t stride);

    /* Calculate the middle half of the iMDCT */
    void (*imdct_half)(struct MDCT15Context *s, float *dst, const float *src,
                       ptrdiff_t stride);
} MDCT15Context;

/* Init an (i)MDCT of the length 2 * 15 * (2^N) */
int ff_mdct15_init(MDCT15Context **ps, int inverse, int N, double scale);
void ff_mdct15_uninit(MDCT15Context **ps);

void ff_mdct15_init_x86(MDCT15Context *s);

#endif /* AVCODEC_MDCT15_H */

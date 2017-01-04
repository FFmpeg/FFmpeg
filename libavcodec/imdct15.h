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

#ifndef AVCODEC_IMDCT15_H
#define AVCODEC_IMDCT15_H

#include <stddef.h>

#include "fft.h"

typedef struct IMDCT15Context {
    int fft_n;
    int len2;
    int len4;
    int *pfa_prereindex;
    int *pfa_postreindex;

    FFTContext ptwo_fft;

    FFTComplex *tmp;

    FFTComplex *twiddle_exptab;

    /* 0 - 18: fft15 twiddles, 19 - 20: fft5 twiddles */
    FFTComplex exptab[21];

    /**
     * Calculate the middle half of the iMDCT
     */
    void (*imdct_half)(struct IMDCT15Context *s, float *dst, const float *src,
                       ptrdiff_t src_stride, float scale);
} IMDCT15Context;

/**
 * Init an iMDCT of the length 2 * 15 * (2^N)
 */
int ff_imdct15_init(IMDCT15Context **s, int N);

/**
 * Free an iMDCT.
 */
void ff_imdct15_uninit(IMDCT15Context **s);

#endif /* AVCODEC_IMDCT15_H */

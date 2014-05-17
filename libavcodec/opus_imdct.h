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

#ifndef AVCODEC_OPUS_IMDCT_H
#define AVCODEC_OPUS_IMDCT_H

#include <stddef.h>

#include "avfft.h"

typedef struct CeltIMDCTContext {
    int fft_n;
    int len2;
    int len4;

    FFTComplex *tmp;

    FFTComplex *twiddle_exptab;

    FFTComplex *exptab[6];

    /**
     * Calculate the middle half of the iMDCT
     */
    void (*imdct_half)(struct CeltIMDCTContext *s, float *dst, const float *src,
                       ptrdiff_t src_stride, float scale);
} CeltIMDCTContext;

/**
 * Init an iMDCT of the length 2 * 15 * (2^N)
 */
int ff_celt_imdct_init(CeltIMDCTContext **s, int N);

/**
 * Free an iMDCT.
 */
void ff_celt_imdct_uninit(CeltIMDCTContext **s);


void ff_celt_imdct_init_aarch64(CeltIMDCTContext *s);

#endif /* AVCODEC_OPUS_IMDCT_H */

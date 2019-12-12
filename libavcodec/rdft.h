/*
 * (I)RDFT transforms
 * Copyright (c) 2009 Alex Converse <alex dot converse at gmail dot com>
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

#if !defined(AVCODEC_RDFT_H) && (!defined(FFT_FLOAT) || FFT_FLOAT)
#define AVCODEC_RDFT_H

#include "config.h"
#include "fft.h"

struct RDFTContext {
    int nbits;
    int inverse;
    int sign_convention;

    /* pre/post rotation tables */
    const FFTSample *tcos;
    const FFTSample *tsin;
    int negative_sin;
    FFTContext fft;
    void (*rdft_calc)(struct RDFTContext *s, FFTSample *z);
};

/**
 * Set up a real FFT.
 * @param nbits           log2 of the length of the input array
 * @param trans           the type of transform
 */
int ff_rdft_init(RDFTContext *s, int nbits, enum RDFTransformType trans);
void ff_rdft_end(RDFTContext *s);

void ff_rdft_init_arm(RDFTContext *s);


#endif /* AVCODEC_RDFT_H */

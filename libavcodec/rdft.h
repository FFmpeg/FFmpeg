/*
 * (I)RDFT transforms
 * Copyright (c) 2009 Alex Converse <alex dot converse at gmail dot com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_RDFT_H
#define AVCODEC_RDFT_H

#include "config.h"
#include "fft.h"

#if CONFIG_HARDCODED_TABLES
#   define SINTABLE_CONST const
#else
#   define SINTABLE_CONST
#endif

#define SINTABLE(size) \
    SINTABLE_CONST DECLARE_ALIGNED(16, FFTSample, ff_sin_##size)[size/2]

extern SINTABLE(16);
extern SINTABLE(32);
extern SINTABLE(64);
extern SINTABLE(128);
extern SINTABLE(256);
extern SINTABLE(512);
extern SINTABLE(1024);
extern SINTABLE(2048);
extern SINTABLE(4096);
extern SINTABLE(8192);
extern SINTABLE(16384);
extern SINTABLE(32768);
extern SINTABLE(65536);

struct RDFTContext {
    int nbits;
    int inverse;
    int sign_convention;

    /* pre/post rotation tables */
    const FFTSample *tcos;
    SINTABLE_CONST FFTSample *tsin;
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

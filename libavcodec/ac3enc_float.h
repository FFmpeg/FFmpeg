/*
 * The simplest AC-3 encoder
 * Copyright (c) 2000 Fabrice Bellard
 * Copyright (c) 2006-2010 Justin Ruggles <justin.ruggles@gmail.com>
 * Copyright (c) 2006-2010 Prakash Punnoor <prakash@punnoor.de>
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

/**
 * @file
 * floating-point AC-3 encoder header.
 */

#ifndef AVCODEC_AC3ENC_FLOAT_H
#define AVCODEC_AC3ENC_FLOAT_H

#include "fft.h"


typedef float SampleType;
typedef float CoefType;
typedef float CoefSumType;


typedef struct AC3MDCTContext {
    const float *window;    ///< MDCT window function
    FFTContext fft;         ///< FFT context for MDCT calculation
} AC3MDCTContext;

#endif /* AVCODEC_AC3ENC_FLOAT_H */

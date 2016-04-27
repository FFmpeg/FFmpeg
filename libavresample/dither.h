/*
 * Copyright (c) 2012 Justin Ruggles <justin.ruggles@gmail.com>
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

#ifndef AVRESAMPLE_DITHER_H
#define AVRESAMPLE_DITHER_H

#include "avresample.h"
#include "audio_data.h"

typedef struct DitherContext DitherContext;

typedef struct DitherDSPContext {
    /**
     * Convert samples from flt to s16 with added dither noise.
     *
     * @param dst    destination float array, range -0.5 to 0.5
     * @param src    source int array, range INT_MIN to INT_MAX.
     * @param dither float dither noise array
     * @param len    number of samples
     */
    void (*quantize)(int16_t *dst, const float *src, float *dither, int len);

    int ptr_align;      ///< src and dst constraints for quantize()
    int samples_align;  ///< len constraints for quantize()

    /**
     * Convert dither noise from int to float with triangular distribution.
     *
     * @param dst  destination float array, range -0.5 to 0.5
     *             constraints: 32-byte aligned
     * @param src0 source int array, range INT_MIN to INT_MAX.
     *             the array size is len * 2
     *             constraints: 32-byte aligned
     * @param len  number of output noise samples
     *             constraints: multiple of 16
     */
    void (*dither_int_to_float)(float *dst, int *src0, int len);
} DitherDSPContext;

/**
 * Allocate and initialize a DitherContext.
 *
 * The parameters in the AVAudioResampleContext are used to initialize the
 * DitherContext.
 *
 * @param avr  AVAudioResampleContext
 * @return     newly-allocated DitherContext
 */
DitherContext *ff_dither_alloc(AVAudioResampleContext *avr,
                               enum AVSampleFormat out_fmt,
                               enum AVSampleFormat in_fmt,
                               int channels, int sample_rate, int apply_map);

/**
 * Free a DitherContext.
 *
 * @param c  DitherContext
 */
void ff_dither_free(DitherContext **c);

/**
 * Convert audio sample format with dithering.
 *
 * @param c    DitherContext
 * @param dst  destination audio data
 * @param src  source audio data
 * @return     0 if ok, negative AVERROR code on failure
 */
int ff_convert_dither(DitherContext *c, AudioData *dst, AudioData *src);

/* arch-specific initialization functions */

void ff_dither_init_x86(DitherDSPContext *ddsp,
                        enum AVResampleDitherMethod method);

#endif /* AVRESAMPLE_DITHER_H */

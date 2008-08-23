/*
 * Lowpass IIR filter
 * Copyright (c) 2008 Konstantin Shishkov
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
 * @file lowpass.h
 * lowpass filter interface
 */

#ifndef FFMPEG_LOWPASS_H
#define FFMPEG_LOWPASS_H

#include "avcodec.h"

struct FFLPFilterCoeffs;
struct FFLPFilterState;

/**
 * Initialize filter coefficients.
 *
 * @param order        filter order
 * @param cutoff_ratio cutoff to input frequency ratio
 *
 * @return pointer to filter coefficients structure or NULL if filter cannot be created
 */
const struct FFLPFilterCoeffs* ff_lowpass_filter_init_coeffs(int order, float cutoff_ratio);

/**
 * Create new filter state.
 *
 * @param order filter order
 *
 * @return pointer to new filter state or NULL if state creation fails
 */
struct FFLPFilterState* ff_lowpass_filter_init_state(int order);

#if 0 //enable with arbitrary order filter implementation, use av_free() for filter state only for now
/**
 * Free filter coefficients.
 *
 * @param coeffs pointer allocated with ff_lowpass_filter_init_coeffs()
 */
void ff_lowpass_filter_free_coeffs(struct FFLPFilterCoeffs *coeffs);

/**
 * Free filter state.
 *
 * @param state pointer allocated with ff_lowpass_filter_init_state()
 */
void ff_lowpass_filter_free_state(struct FFLPFilterState *state);
#endif

/**
 * Perform lowpass filtering on input samples.
 *
 * @param coeffs pointer to filter coefficients
 * @param state  pointer to filter state
 * @param size   input length
 * @param src    source samples
 * @param sstep  source stride
 * @param dst    filtered samples (destination may be the same as input)
 * @param dstep  destination stride
 */
void ff_lowpass_filter(const struct FFLPFilterCoeffs *coeffs, struct FFLPFilterState *state, int size, int16_t *src, int sstep, int16_t *dst, int dstep);

#endif /* FFMPEG_LOWPASS_H */

/*
 * IIR filter
 * Copyright (c) 2008 Konstantin Shishkov
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

/**
 * @file
 * IIR filter interface
 */

#ifndef AVCODEC_IIRFILTER_H
#define AVCODEC_IIRFILTER_H

#include "avcodec.h"

struct FFIIRFilterCoeffs;
struct FFIIRFilterState;

enum IIRFilterType{
    FF_FILTER_TYPE_BESSEL,
    FF_FILTER_TYPE_BIQUAD,
    FF_FILTER_TYPE_BUTTERWORTH,
    FF_FILTER_TYPE_CHEBYSHEV,
    FF_FILTER_TYPE_ELLIPTIC,
};

enum IIRFilterMode{
    FF_FILTER_MODE_LOWPASS,
    FF_FILTER_MODE_HIGHPASS,
    FF_FILTER_MODE_BANDPASS,
    FF_FILTER_MODE_BANDSTOP,
};

/**
 * Initialize filter coefficients.
 *
 * @param avc          a pointer to an arbitrary struct of which the first
 *                     field is a pointer to an AVClass struct
 * @param filt_type    filter type (e.g. Butterworth)
 * @param filt_mode    filter mode (e.g. lowpass)
 * @param order        filter order
 * @param cutoff_ratio cutoff to input frequency ratio
 * @param stopband     stopband to input frequency ratio (used by bandpass and bandstop filter modes)
 * @param ripple       ripple factor (used only in Chebyshev filters)
 *
 * @return pointer to filter coefficients structure or NULL if filter cannot be created
 */
struct FFIIRFilterCoeffs* ff_iir_filter_init_coeffs(void *avc,
                                                enum IIRFilterType filt_type,
                                                enum IIRFilterMode filt_mode,
                                                int order, float cutoff_ratio,
                                                float stopband, float ripple);

/**
 * Create new filter state.
 *
 * @param order filter order
 *
 * @return pointer to new filter state or NULL if state creation fails
 */
struct FFIIRFilterState* ff_iir_filter_init_state(int order);

/**
 * Free filter coefficients.
 *
 * @param coeffs pointer allocated with ff_iir_filter_init_coeffs()
 */
void ff_iir_filter_free_coeffs(struct FFIIRFilterCoeffs *coeffs);

/**
 * Free filter state.
 *
 * @param state pointer allocated with ff_iir_filter_init_state()
 */
void ff_iir_filter_free_state(struct FFIIRFilterState *state);

/**
 * Perform IIR filtering on signed 16-bit input samples.
 *
 * @param coeffs pointer to filter coefficients
 * @param state  pointer to filter state
 * @param size   input length
 * @param src    source samples
 * @param sstep  source stride
 * @param dst    filtered samples (destination may be the same as input)
 * @param dstep  destination stride
 */
void ff_iir_filter(const struct FFIIRFilterCoeffs *coeffs, struct FFIIRFilterState *state,
                   int size, const int16_t *src, int sstep, int16_t *dst, int dstep);

/**
 * Perform IIR filtering on floating-point input samples.
 *
 * @param coeffs pointer to filter coefficients
 * @param state  pointer to filter state
 * @param size   input length
 * @param src    source samples
 * @param sstep  source stride
 * @param dst    filtered samples (destination may be the same as input)
 * @param dstep  destination stride
 */
void ff_iir_filter_flt(const struct FFIIRFilterCoeffs *coeffs,
                       struct FFIIRFilterState *state, int size,
                       const float *src, int sstep, float *dst, int dstep);

#endif /* AVCODEC_IIRFILTER_H */

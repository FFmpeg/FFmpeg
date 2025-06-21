/*
 * Copyright (c) 2012 Clément Bœsch
 * Copyright (c) 2025 Niklas Haas
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

#ifndef AVFILTER_F_EBUR128_H
#define AVFILTER_F_EBUR128_H

#include <assert.h>
#include <stddef.h>

typedef struct EBUR128Biquad {
    double b0, b1, b2;
    double a1, a2;
} EBUR128Biquad;

typedef struct EBUR128DSPContext {
    /* Filter data */
    EBUR128Biquad pre;
    EBUR128Biquad rlb;

    /* Cache of 3 samples for each channel */
    double *y; /* after pre-filter */
    double *z; /* after RLB-filter */

    /* DSP functions */
    void (*filter_channels)(const struct EBUR128DSPContext *dsp,
                            const double *samples,
                            double *cache_400, double *cache_3000,
                            double *sum_400, double *sum_3000,
                            int nb_channels);

    /* Updates ch_peaks[] and returns maximum from all channels */
    double (*find_peak)(double *ch_peaks, int nb_channels,
                        const double *samples, int nb_samples);
} EBUR128DSPContext;

static_assert(offsetof(EBUR128DSPContext, pre) == 0,                   "struct layout mismatch");
static_assert(offsetof(EBUR128DSPContext, rlb) == 5  * sizeof(double), "struct layout mismatch");
static_assert(offsetof(EBUR128DSPContext, y)   == 10 * sizeof(double), "struct layout mismatch");

void ff_ebur128_init_x86(EBUR128DSPContext *dsp, int nb_channels);

void ff_ebur128_filter_channels_c(const EBUR128DSPContext *, const double *,
                                  double *, double *, double *, double *, int);

double ff_ebur128_find_peak_c(double *, int, const double *, int);

#endif /* AVFILTER_F_EBUR128_H */

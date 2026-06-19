/*
 * Copyright (C) 2026 Niklas Haas
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

#ifndef SWSCALE_FILTERS_H
#define SWSCALE_FILTERS_H

#include <libavutil/refstruct.h>

#include "swscale.h"

/* Design constants of the filtering subsystem */
enum {
    /**
     * 14-bit coefficients are picked to fit comfortably within int16_t
     * for efficient SIMD processing (e.g. pmaddwd on x86). Conversely, this
     * limits the maximum filter size to 256, to avoid excessive precision
     * loss. (Consider that 14 - 8 = 6 bit effective weight resolution)
     *
     * Note that this limitation would not apply to floating-point filters,
     * and in a future update to this code, we could gain the ability to
     * generate unbounded floating point filters directly.
     */
    SWS_FILTER_SCALE    = (1 << 14),
    SWS_FILTER_SIZE_MAX = 256,
};

/* Parameters for filter generation. */
typedef struct SwsFilterParams {
    /**
     * The filter kernel and parameters to use.
     */
    SwsScaler scaler;
    double scaler_params[SWS_NUM_SCALER_PARAMS];

    /**
     * The relative sizes of the input and output images. Used to determine
     * the number of rows in the output, as well as the fractional offsets of
     * the samples in each row.
     */
    int src_size;
    int dst_size;

    /**
     * The virtual output size. If zero, this is assumed to be the same as
     * `dst_size`. Matters for e.g. chroma subsampling, where the the luma
     * plane may be smaller than the dst_size. For example, a 99x99 input
     * image has a chroma size of 50x50, which would be 100x100 after
     * chroma upscaling; but is sampled only at 99x99 resolution. In this
     * instance, dst_size is 99x99 and virtual_size is 100x100.
     *
     * The upscaling offset from this shift is implicit and does not need
     * to be accounted for in `offset`. In other words, `offset` is taken
     * relative to the virtual size, not the sampled size.
     */
    double virtual_size;

    /**
     * The sample offset, in units of input pixels. This is added onto all
     * sampled coordinates directly, i.e. a value of offset = 1.0 would shift
     * the output to the top/left by one whole source pixel.
     */
    double offset;
} SwsFilterParams;

/**
 * Represents a computed filter kernel.
 */
typedef struct SwsFilterWeights {
    /**
     * The number of source texels to convolve over for each row.
     */
    int filter_size;

    /**
     * The computed look-up table (LUT). This is interpreted as a 2D array with
     * dimensions [dst_height][row_size]. The inner rows contain the `row_size`
     * samples to convolve with the corresponding input pixels. The outer
     * coordinate is indexed by the position of the sample to reconstruct.
     */
    int *weights; /* refstruct */
    size_t num_weights;

    /**
     * The computed source pixel positions for each row of the filter. This
     * indexes into the source image, and gives the position of the first
     * source pixel to convolve with for each entry.
     */
    int *offsets; /* refstruct */

    /**
     * Copy of the parameters used to generate this filter, for reference.
     */
    int src_size;
    int dst_size;
    double virtual_size;
    double offset;

    /**
     * Extra metadata about the filter, used to inform the optimizer / range
     * tracker about the filter's behavior.
     */
    char name[16];    /* name of the configured filter kernel */
    int sum_positive; /* (maximum) sum of all positive weights */
    int sum_negative; /* (minimum) sum of all negative weights */
} SwsFilterWeights;

/**
 * Generate a filter kernel for the given parameters. The generated filter is
 * allocated as a refstruct and must be unref'd by the caller.
 *
 * Returns 0 or a negative error code. In particular, this may return:
 * - AVERROR(ENOMEM) if memory allocation fails.
 * - AVERROR(EINVAL) if the provided parameters are invalid (e.g. out of range).
 * - AVERROR(ENOTSUP) if the generated filter would exceed SWS_FILTER_SIZE_MAX.
 **/
int ff_sws_filter_generate(void *log_ctx, const SwsFilterParams *params,
                           SwsFilterWeights **out);

#endif /* SWSCALE_FILTERS_H */

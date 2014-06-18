/*
 * audio resampling
 * Copyright (c) 2004-2012 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef SWRESAMPLE_RESAMPLE_H
#define SWRESAMPLE_RESAMPLE_H

#include "libavutil/log.h"
#include "libavutil/samplefmt.h"

#include "swresample_internal.h"

typedef void (*resample_one_fn)(uint8_t *dst, const uint8_t *src,
                                int n, int64_t index, int64_t incr);
typedef int (*resample_fn)(struct ResampleContext *c, uint8_t *dst,
                           const uint8_t *src, int n, int update_ctx);

typedef struct ResampleContext {
    const AVClass *av_class;
    uint8_t *filter_bank;
    int filter_length;
    int filter_alloc;
    int ideal_dst_incr;
    int dst_incr;
    int dst_incr_div;
    int dst_incr_mod;
    int index;
    int frac;
    int src_incr;
    int compensation_distance;
    int phase_shift;
    int phase_mask;
    int linear;
    enum SwrFilterType filter_type;
    int kaiser_beta;
    double factor;
    enum AVSampleFormat format;
    int felem_size;
    int filter_shift;

    struct {
        resample_one_fn resample_one[AV_SAMPLE_FMT_NB - AV_SAMPLE_FMT_S16P];
        resample_fn resample_common[AV_SAMPLE_FMT_NB - AV_SAMPLE_FMT_S16P];
        resample_fn resample_linear[AV_SAMPLE_FMT_NB - AV_SAMPLE_FMT_S16P];
    } dsp;
} ResampleContext;

void swresample_dsp_init(ResampleContext *c);
void swresample_dsp_x86_init(ResampleContext *c);

#endif /* SWRESAMPLE_RESAMPLE_H */

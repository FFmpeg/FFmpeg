/*
 * Audio resampling
 *
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

#include "config.h"

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/avassert.h"

#include "libavutil/aarch64/cpu.h"
#include "libswresample/resample.h"

#define DECLARE_RESAMPLE_COMMON_TEMPLATE(TYPE, DELEM, FELEM, FELEM2, OUT)                         \
                                                                                                  \
void ff_resample_common_apply_filter_x4_##TYPE##_neon(FELEM2 *acc, const DELEM *src,              \
                                                      const FELEM *filter, int length);           \
                                                                                                  \
void ff_resample_common_apply_filter_x8_##TYPE##_neon(FELEM2 *acc, const DELEM *src,              \
                                                      const FELEM *filter, int length);           \
                                                                                                  \
static int ff_resample_common_##TYPE##_neon(ResampleContext *c, void *dest, const void *source,   \
                                            int n, int update_ctx)                                \
{                                                                                                 \
    DELEM *dst = dest;                                                                            \
    const DELEM *src = source;                                                                    \
    int dst_index;                                                                                \
    int index = c->index;                                                                         \
    int frac = c->frac;                                                                           \
    int sample_index = 0;                                                                         \
    int x4_aligned_filter_length = c->filter_length & ~3;                                         \
    int x8_aligned_filter_length = c->filter_length & ~7;                                         \
                                                                                                  \
    while (index >= c->phase_count) {                                                             \
        sample_index++;                                                                           \
        index -= c->phase_count;                                                                  \
    }                                                                                             \
                                                                                                  \
    for (dst_index = 0; dst_index < n; dst_index++) {                                             \
        FELEM *filter = ((FELEM *) c->filter_bank) + c->filter_alloc * index;                     \
                                                                                                  \
        FELEM2 val = 0;                                                                             \
        int i = 0;                                                                                \
        if (x8_aligned_filter_length >= 8) {                                                      \
            ff_resample_common_apply_filter_x8_##TYPE##_neon(&val, &src[sample_index],            \
                                                             filter, x8_aligned_filter_length);   \
            i += x8_aligned_filter_length;                                                        \
                                                                                                  \
        } else if (x4_aligned_filter_length >= 4) {                                               \
            ff_resample_common_apply_filter_x4_##TYPE##_neon(&val, &src[sample_index],            \
                                                             filter, x4_aligned_filter_length);   \
            i += x4_aligned_filter_length;                                                        \
        }                                                                                         \
        for (; i < c->filter_length; i++) {                                                       \
            val += src[sample_index + i] * (FELEM2)filter[i];                                     \
        }                                                                                         \
        OUT(dst[dst_index], val);                                                                 \
                                                                                                  \
        frac  += c->dst_incr_mod;                                                                 \
        index += c->dst_incr_div;                                                                 \
        if (frac >= c->src_incr) {                                                                \
            frac -= c->src_incr;                                                                  \
            index++;                                                                              \
        }                                                                                         \
                                                                                                  \
        while (index >= c->phase_count) {                                                         \
            sample_index++;                                                                       \
            index -= c->phase_count;                                                              \
        }                                                                                         \
    }                                                                                             \
                                                                                                  \
    if (update_ctx) {                                                                             \
        c->frac = frac;                                                                           \
        c->index = index;                                                                         \
    }                                                                                             \
                                                                                                  \
    return sample_index;                                                                          \
}                                                                                                 \

#define OUT(d, v) d = v
DECLARE_RESAMPLE_COMMON_TEMPLATE(float, float, float, float, OUT)
#undef OUT

#define OUT(d, v) (v) = ((v) + (1<<(14)))>>15; (d) = av_clip_int16(v)
DECLARE_RESAMPLE_COMMON_TEMPLATE(s16, int16_t, int16_t, int32_t, OUT)
#undef OUT

av_cold void swri_resample_dsp_aarch64_init(ResampleContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (!have_neon(cpu_flags))
        return;

    switch(c->format) {
    case AV_SAMPLE_FMT_FLTP:
        c->dsp.resample_common = ff_resample_common_float_neon;
        break;
    case AV_SAMPLE_FMT_S16P:
        c->dsp.resample_common = ff_resample_common_s16_neon;
        break;
    }
}

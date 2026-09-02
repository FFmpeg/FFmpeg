/*
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

#ifndef AVFILTER_COLORDETECTDSP_H
#define AVFILTER_COLORDETECTDSP_H

#include <stddef.h>
#include <stdint.h>

#include "config.h"

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/pixfmt.h"

enum FFAlphaDetect {
    FF_ALPHA_NONE         = -1,
    FF_ALPHA_UNDETERMINED = 0,
    FF_ALPHA_TRANSPARENT  = 1 << 0, ///< alpha < alpha_max
    FF_ALPHA_STRAIGHT     = (1 << 1) | FF_ALPHA_TRANSPARENT, ///< alpha < pixel
    /* No way to positively identify premultiplied alpha */
};

typedef struct FFColorDetectDSPContext {
    /* Returns 1 if an out-of-range value was detected, 0 otherwise */
    int (*detect_range)(const uint8_t *data, ptrdiff_t stride,
                        ptrdiff_t width, ptrdiff_t height,
                        int mpeg_min, int mpeg_max);

    /* Returns an FFAlphaDetect enum value */
    int (*detect_alpha)(const uint8_t *color, ptrdiff_t color_stride,
                        const uint8_t *alpha, ptrdiff_t alpha_stride,
                        ptrdiff_t width, ptrdiff_t height,
                        int alpha_max, int mpeg_range, int offset);
} FFColorDetectDSPContext;

void ff_color_detect_dsp_init_aarch64(FFColorDetectDSPContext *dsp, int depth,
                                      enum AVColorRange color_range);
void ff_color_detect_dsp_init_x86(FFColorDetectDSPContext *dsp, int depth,
                                  int offset, enum AVColorRange color_range);

#define DECL_DETECT_RANGE_IMPL(TYPE, NAME)                                     \
static inline int NAME(const uint8_t* data, ptrdiff_t stride,                  \
                       ptrdiff_t width, ptrdiff_t height,                      \
                       int mpeg_min, int mpeg_max)                             \
{                                                                              \
    const TYPE min = mpeg_min;                                                 \
    const TYPE max = mpeg_max;                                                 \
    av_assume(min == mpeg_min);                                                \
    av_assume(max == mpeg_max);                                                \
                                                                               \
    while (height--) {                                                         \
        const TYPE *row = (const TYPE *) data;                                 \
        uint8_t cond = 0;                                                      \
        for (int x = 0; x < width; x++) {                                      \
            const TYPE val = row[x];                                           \
            cond |= val < min || val > max;                                    \
        }                                                                      \
        if (cond)                                                              \
            return 1;                                                          \
        data += stride;                                                        \
    }                                                                          \
                                                                               \
    return 0;                                                                  \
}

DECL_DETECT_RANGE_IMPL(uint8_t,  ff_detect_range_c)
DECL_DETECT_RANGE_IMPL(uint16_t, ff_detect_range16_c)

#define DECL_DETECT_ALPHA_FULL_IMPL(TYPE, INTER, NAME)                         \
static inline int NAME(const uint8_t* color, ptrdiff_t color_stride,           \
                       const uint8_t* alpha, ptrdiff_t alpha_stride,           \
                       ptrdiff_t width, ptrdiff_t height, int alpha_max,       \
                       int mpeg_range, int offset)                             \
{                                                                              \
    const TYPE  max = alpha_max;                                               \
    const INTER off = offset;                                                  \
    av_assume(max == alpha_max);                                               \
    av_assume(off == offset);                                                  \
    (void) mpeg_range;                                                         \
                                                                               \
    uint8_t transparent = 0;                                                   \
    while (height--) {                                                         \
        const TYPE *col = (const TYPE *) color;                                \
        const TYPE *alp = (const TYPE *) alpha;                                \
        uint8_t straight = 0;                                                  \
        for (int x = 0; x < width; x++) {                                      \
            straight |= col[x] > alp[x] + off;                                 \
            transparent |= alp[x] != max;                                      \
        }                                                                      \
        if (straight)                                                          \
            return FF_ALPHA_STRAIGHT;                                          \
        color += color_stride;                                                 \
        alpha += alpha_stride;                                                 \
    }                                                                          \
    return transparent ? FF_ALPHA_TRANSPARENT : 0;                             \
}

DECL_DETECT_ALPHA_FULL_IMPL(uint8_t,  uint16_t, ff_detect_alpha_full_c)
DECL_DETECT_ALPHA_FULL_IMPL(uint16_t, uint32_t, ff_detect_alpha16_full_c)

#define DECL_DETECT_ALPHA_LIMITED_IMPL(TYPE, INTER, NAME)                      \
static inline int NAME(const uint8_t* color, ptrdiff_t color_stride,           \
                       const uint8_t* alpha, ptrdiff_t alpha_stride,           \
                       ptrdiff_t width, ptrdiff_t height, int alpha_max,       \
                       int mpeg_range, int offset)                             \
{                                                                              \
    const TYPE  max = alpha_max;                                               \
    const INTER off = offset;                                                  \
    const INTER range = mpeg_range;                                            \
    av_assume(max == alpha_max);                                               \
    av_assume(off == offset);                                                  \
    av_assume(range == mpeg_range);                                            \
                                                                               \
    uint8_t transparent = 0;                                                   \
    while (height--) {                                                         \
        const TYPE *col = (const TYPE *) color;                                \
        const TYPE *alp = (const TYPE *) alpha;                                \
        uint8_t straight = 0;                                                  \
        for (int x = 0; x < width; x++) {                                      \
            straight |= (INTER) max * col[x] - off > range * alp[x];           \
            transparent |= alp[x] != max;                                      \
        }                                                                      \
        if (straight)                                                          \
            return FF_ALPHA_STRAIGHT;                                          \
        color += color_stride;                                                 \
        alpha += alpha_stride;                                                 \
    }                                                                          \
    return transparent ? FF_ALPHA_TRANSPARENT : 0;                             \
}

DECL_DETECT_ALPHA_LIMITED_IMPL(uint8_t,  int32_t, ff_detect_alpha_limited_c)
DECL_DETECT_ALPHA_LIMITED_IMPL(uint16_t, int64_t, ff_detect_alpha16_limited_c)

static av_cold inline void
ff_color_detect_dsp_init(FFColorDetectDSPContext *dsp, int depth, int offset,
                         enum AVColorRange color_range)
{
    dsp->detect_range = depth > 8 ? ff_detect_range16_c : ff_detect_range_c;
    if (color_range == AVCOL_RANGE_JPEG) {
        dsp->detect_alpha = depth > 8 ? ff_detect_alpha16_full_c : ff_detect_alpha_full_c;
    } else {
        dsp->detect_alpha = depth > 8 ? ff_detect_alpha16_limited_c : ff_detect_alpha_limited_c;
    }

#if ARCH_AARCH64
    ff_color_detect_dsp_init_aarch64(dsp, depth, color_range);
#elif ARCH_X86 && HAVE_X86ASM
    ff_color_detect_dsp_init_x86(dsp, depth, offset, color_range);
#endif
}

#endif /* AVFILTER_COLORDETECTDSP_H */

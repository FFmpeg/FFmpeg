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

#ifndef AVFILTER_COLORDETECT_H
#define AVFILTER_COLORDETECT_H

#include <stddef.h>
#include <stdint.h>

#include <libavutil/avassert.h>
#include <libavutil/macros.h>
#include <libavutil/pixfmt.h>

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

void ff_color_detect_dsp_init(FFColorDetectDSPContext *dsp, int depth,
                              enum AVColorRange color_range);

void ff_color_detect_dsp_init_x86(FFColorDetectDSPContext *dsp, int depth,
                                  enum AVColorRange color_range);

static inline int ff_detect_range_impl_c(const uint8_t *data, ptrdiff_t stride,
                                    ptrdiff_t width, ptrdiff_t height,
                                    uint8_t mpeg_min, uint8_t mpeg_max)
{
    while (height--) {
        uint8_t cond = 0;
        for (int x = 0; x < width; x++) {
            const uint8_t val = data[x];
            cond |= val < mpeg_min || val > mpeg_max;
        }
        if (cond)
            return 1;
        data += stride;
    }

    return 0;
}

static inline int ff_detect_range_c(const uint8_t *data, ptrdiff_t stride,
                                    ptrdiff_t width, ptrdiff_t height,
                                    int mpeg_min, int mpeg_max)
{
    av_assume(mpeg_min >= 0 && mpeg_min <= UINT8_MAX);
    av_assume(mpeg_max >= 0 && mpeg_max <= UINT8_MAX);
    return ff_detect_range_impl_c(data, stride, width, height, mpeg_min, mpeg_max);
}

static inline int ff_detect_range16_impl_c(const uint8_t *data, ptrdiff_t stride,
                                      ptrdiff_t width, ptrdiff_t height,
                                      uint16_t mpeg_min, uint16_t mpeg_max)
{
    while (height--) {
        const uint16_t *data16 = (const uint16_t *) data;
        uint8_t cond = 0;
        for (int x = 0; x < width; x++) {
            const uint16_t val = data16[x];
            cond |= val < mpeg_min || val > mpeg_max;
        }
        if (cond)
            return 1;
        data += stride;
    }

    return 0;
}

static inline int ff_detect_range16_c(const uint8_t *data, ptrdiff_t stride,
                                      ptrdiff_t width, ptrdiff_t height,
                                      int mpeg_min, int mpeg_max)
{
    av_assume(mpeg_min >= 0 && mpeg_min <= UINT16_MAX);
    av_assume(mpeg_max >= 0 && mpeg_max <= UINT16_MAX);
    return ff_detect_range16_impl_c(data, stride, width, height, mpeg_min, mpeg_max);
}

static inline int
ff_detect_alpha_full_c(const uint8_t *color, ptrdiff_t color_stride,
                       const uint8_t *alpha, ptrdiff_t alpha_stride,
                       ptrdiff_t width, ptrdiff_t height,
                       int alpha_max, int mpeg_range, int offset)
{
    uint8_t transparent = 0;
    while (height--) {
        uint8_t straight = 0;
        for (int x = 0; x < width; x++) {
            straight  |= color[x] > alpha[x];
            transparent |= alpha[x] != alpha_max;
        }
        if (straight)
            return FF_ALPHA_STRAIGHT;
        color += color_stride;
        alpha += alpha_stride;
    }
    return transparent ? FF_ALPHA_TRANSPARENT : 0;
}

static inline int
ff_detect_alpha_limited_c(const uint8_t *color, ptrdiff_t color_stride,
                          const uint8_t *alpha, ptrdiff_t alpha_stride,
                          ptrdiff_t width, ptrdiff_t height,
                          int alpha_max, int mpeg_range, int offset)
{
    uint8_t transparent = 0;
    while (height--) {
        uint8_t straight = 0;
        for (int x = 0; x < width; x++) {
            straight  |= alpha_max * color[x] - offset > mpeg_range * alpha[x];
            transparent |= alpha[x] != alpha_max;
        }
        if (straight)
            return FF_ALPHA_STRAIGHT;
        color += color_stride;
        alpha += alpha_stride;
    }
    return transparent ? FF_ALPHA_TRANSPARENT : 0;
}

static inline int
ff_detect_alpha16_full_c(const uint8_t *color, ptrdiff_t color_stride,
                         const uint8_t *alpha, ptrdiff_t alpha_stride,
                         ptrdiff_t width, ptrdiff_t height,
                         int alpha_max, int mpeg_range, int offset)
{
    uint8_t transparent = 0;
    while (height--) {
        const uint16_t *color16 = (const uint16_t *) color;
        const uint16_t *alpha16 = (const uint16_t *) alpha;
        uint8_t straight = 0;
        for (int x = 0; x < width; x++) {
            straight  |= color16[x] > alpha16[x];
            transparent |= alpha16[x] != alpha_max;
        }
        if (straight)
            return FF_ALPHA_STRAIGHT;
        color += color_stride;
        alpha += alpha_stride;
    }
    return transparent ? FF_ALPHA_TRANSPARENT : 0;
}

static inline int
ff_detect_alpha16_limited_c(const uint8_t *color, ptrdiff_t color_stride,
                            const uint8_t *alpha, ptrdiff_t alpha_stride,
                            ptrdiff_t width, ptrdiff_t height,
                            int alpha_max, int mpeg_range, int offset)
{
    uint8_t transparent = 0;
    while (height--) {
        const uint16_t *color16 = (const uint16_t *) color;
        const uint16_t *alpha16 = (const uint16_t *) alpha;
        for (int x = 0; x < width; x++) {
            if ((int64_t) alpha_max * color16[x] - offset > (int64_t) mpeg_range * alpha16[x])
                return FF_ALPHA_STRAIGHT;
            transparent |= alpha16[x] != alpha_max;
        }
        color += color_stride;
        alpha += alpha_stride;
    }
    return transparent ? FF_ALPHA_TRANSPARENT : 0;
}

#endif /* AVFILTER_COLORDETECT_H */

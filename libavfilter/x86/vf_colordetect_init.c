/*
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

#include "libavutil/attributes.h"
#include "libavutil/x86/cpu.h"
#include "libavfilter/vf_colordetect.h"

#define DETECT_RANGE_FUNC(FUNC_NAME, ASM_FUNC_NAME, C_FUNC_NAME, SHIFT, MMSIZE) \
int ASM_FUNC_NAME(const uint8_t *src, ptrdiff_t stride,                         \
                  ptrdiff_t width, ptrdiff_t height, int min, int max);         \
                                                                                \
static int FUNC_NAME(const uint8_t *src, ptrdiff_t stride,                      \
                     ptrdiff_t width, ptrdiff_t height, int min, int max)       \
{                                                                               \
    ptrdiff_t bytes = (width << SHIFT) & ~(MMSIZE - 1);                         \
    int ret = ASM_FUNC_NAME(src, stride, bytes, height, min, max);              \
    if (ret)                                                                    \
        return ret;                                                             \
                                                                                \
    return C_FUNC_NAME(src + bytes, stride, width - (bytes >> SHIFT),           \
                       height, min, max);                                       \
}

#define DETECT_ALPHA_FUNC(FUNC_NAME, ASM_FUNC_NAME, C_FUNC_NAME, SHIFT, MMSIZE) \
int ASM_FUNC_NAME(const uint8_t *color, ptrdiff_t color_stride,                 \
                  const uint8_t *alpha, ptrdiff_t alpha_stride,                 \
                  ptrdiff_t width, ptrdiff_t height, int p, int q, int k);      \
                                                                                \
static int FUNC_NAME(const uint8_t *color, ptrdiff_t color_stride,              \
                     const uint8_t *alpha, ptrdiff_t alpha_stride,              \
                     ptrdiff_t width, ptrdiff_t height, int p, int q, int k)    \
{                                                                               \
    ptrdiff_t bytes = (width << SHIFT) & ~(MMSIZE - 1);                         \
    int ret = ASM_FUNC_NAME(color, color_stride, alpha, alpha_stride,           \
                            bytes, height, p, q, k);                            \
    if (ret)                                                                    \
        return ret;                                                             \
                                                                                \
    return C_FUNC_NAME(color + bytes, color_stride, alpha + bytes, alpha_stride,\
                       width - (bytes >> SHIFT), height, p, q, k);              \
}

#if HAVE_X86ASM
#if HAVE_AVX512ICL_EXTERNAL
DETECT_RANGE_FUNC(detect_range_avx512icl,   ff_detect_rangeb_avx512icl, ff_detect_range_c,   0, 64)
DETECT_RANGE_FUNC(detect_range16_avx512icl, ff_detect_rangew_avx512icl, ff_detect_range16_c, 1, 64)
DETECT_ALPHA_FUNC(detect_alpha_full_avx512icl,   ff_detect_alphab_full_avx512icl, ff_detect_alpha_full_c,   0, 64)
DETECT_ALPHA_FUNC(detect_alpha16_full_avx512icl, ff_detect_alphaw_full_avx512icl, ff_detect_alpha16_full_c, 1, 64)
DETECT_ALPHA_FUNC(detect_alpha_limited_avx512icl,   ff_detect_alphab_limited_avx512icl, ff_detect_alpha_limited_c,   0, 64)
DETECT_ALPHA_FUNC(detect_alpha16_limited_avx512icl, ff_detect_alphaw_limited_avx512icl, ff_detect_alpha16_limited_c, 1, 64)
#endif
#if HAVE_AVX2_EXTERNAL
DETECT_RANGE_FUNC(detect_range_avx2,   ff_detect_rangeb_avx2, ff_detect_range_c,   0, 32)
DETECT_RANGE_FUNC(detect_range16_avx2, ff_detect_rangew_avx2, ff_detect_range16_c, 1, 32)
DETECT_ALPHA_FUNC(detect_alpha_full_avx2,   ff_detect_alphab_full_avx2, ff_detect_alpha_full_c,   0, 32)
DETECT_ALPHA_FUNC(detect_alpha16_full_avx2, ff_detect_alphaw_full_avx2, ff_detect_alpha16_full_c, 1, 32)
DETECT_ALPHA_FUNC(detect_alpha_limited_avx2,   ff_detect_alphab_limited_avx2, ff_detect_alpha_limited_c,   0, 32)
DETECT_ALPHA_FUNC(detect_alpha16_limited_avx2, ff_detect_alphaw_limited_avx2, ff_detect_alpha16_limited_c, 1, 32)
#endif
#endif

av_cold void ff_color_detect_dsp_init_x86(FFColorDetectDSPContext *dsp, int depth,
                                          enum AVColorRange color_range)
{
#if HAVE_X86ASM
    int cpu_flags = av_get_cpu_flags();
#if HAVE_AVX2_EXTERNAL
    if (EXTERNAL_AVX2_FAST(cpu_flags)) {
        dsp->detect_range = depth > 8 ? detect_range16_avx2 : detect_range_avx2;
        if (color_range == AVCOL_RANGE_JPEG) {
            dsp->detect_alpha = depth > 8 ? detect_alpha16_full_avx2 : detect_alpha_full_avx2;
        } else {
            dsp->detect_alpha = depth > 8 ? detect_alpha16_limited_avx2 : detect_alpha_limited_avx2;
        }
    }
#endif
#if HAVE_AVX512ICL_EXTERNAL
    if (EXTERNAL_AVX512ICL(cpu_flags)) {
        dsp->detect_range = depth > 8 ? detect_range16_avx512icl : detect_range_avx512icl;
        if (color_range == AVCOL_RANGE_JPEG) {
            dsp->detect_alpha = depth > 8 ? detect_alpha16_full_avx512icl : detect_alpha_full_avx512icl;
        } else {
            dsp->detect_alpha = depth > 8 ? detect_alpha16_limited_avx512icl : detect_alpha_limited_avx512icl;
        }
    }
#endif
#endif
}

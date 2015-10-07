/*
 * Copyright (c) 2015 Paul B Mahol
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
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavfilter/blend.h"

void ff_blend_addition_sse2(const uint8_t *top, ptrdiff_t top_linesize,
                            const uint8_t *bottom, ptrdiff_t bottom_linesize,
                            uint8_t *dst, ptrdiff_t dst_linesize,
                            ptrdiff_t width, ptrdiff_t start, ptrdiff_t end,
                            struct FilterParams *param, double *values);

void ff_blend_addition128_sse2(const uint8_t *top, ptrdiff_t top_linesize,
                               const uint8_t *bottom, ptrdiff_t bottom_linesize,
                               uint8_t *dst, ptrdiff_t dst_linesize,
                               ptrdiff_t width, ptrdiff_t start, ptrdiff_t end,
                               struct FilterParams *param, double *values);

void ff_blend_average_sse2(const uint8_t *top, ptrdiff_t top_linesize,
                           const uint8_t *bottom, ptrdiff_t bottom_linesize,
                           uint8_t *dst, ptrdiff_t dst_linesize,
                           ptrdiff_t width, ptrdiff_t start, ptrdiff_t end,
                           struct FilterParams *param, double *values);

void ff_blend_and_sse2(const uint8_t *top, ptrdiff_t top_linesize,
                       const uint8_t *bottom, ptrdiff_t bottom_linesize,
                       uint8_t *dst, ptrdiff_t dst_linesize,
                       ptrdiff_t width, ptrdiff_t start, ptrdiff_t end,
                       struct FilterParams *param, double *values);

void ff_blend_darken_sse2(const uint8_t *top, ptrdiff_t top_linesize,
                          const uint8_t *bottom, ptrdiff_t bottom_linesize,
                          uint8_t *dst, ptrdiff_t dst_linesize,
                          ptrdiff_t width, ptrdiff_t start, ptrdiff_t end,
                          struct FilterParams *param, double *values);

void ff_blend_difference128_sse2(const uint8_t *top, ptrdiff_t top_linesize,
                                 const uint8_t *bottom, ptrdiff_t bottom_linesize,
                                 uint8_t *dst, ptrdiff_t dst_linesize,
                                 ptrdiff_t width, ptrdiff_t start, ptrdiff_t end,
                                 struct FilterParams *param, double *values);

void ff_blend_hardmix_sse2(const uint8_t *top, ptrdiff_t top_linesize,
                           const uint8_t *bottom, ptrdiff_t bottom_linesize,
                           uint8_t *dst, ptrdiff_t dst_linesize,
                           ptrdiff_t width, ptrdiff_t start, ptrdiff_t end,
                           struct FilterParams *param, double *values);

void ff_blend_lighten_sse2(const uint8_t *top, ptrdiff_t top_linesize,
                           const uint8_t *bottom, ptrdiff_t bottom_linesize,
                           uint8_t *dst, ptrdiff_t dst_linesize,
                           ptrdiff_t width, ptrdiff_t start, ptrdiff_t end,
                           struct FilterParams *param, double *values);

void ff_blend_or_sse2(const uint8_t *top, ptrdiff_t top_linesize,
                      const uint8_t *bottom, ptrdiff_t bottom_linesize,
                      uint8_t *dst, ptrdiff_t dst_linesize,
                      ptrdiff_t width, ptrdiff_t start, ptrdiff_t end,
                      struct FilterParams *param, double *values);

void ff_blend_phoenix_sse2(const uint8_t *top, ptrdiff_t top_linesize,
                           const uint8_t *bottom, ptrdiff_t bottom_linesize,
                           uint8_t *dst, ptrdiff_t dst_linesize,
                           ptrdiff_t width, ptrdiff_t start, ptrdiff_t end,
                           struct FilterParams *param, double *values);

void ff_blend_subtract_sse2(const uint8_t *top, ptrdiff_t top_linesize,
                            const uint8_t *bottom, ptrdiff_t bottom_linesize,
                            uint8_t *dst, ptrdiff_t dst_linesize,
                            ptrdiff_t width, ptrdiff_t start, ptrdiff_t end,
                            struct FilterParams *param, double *values);

void ff_blend_xor_sse2(const uint8_t *top, ptrdiff_t top_linesize,
                       const uint8_t *bottom, ptrdiff_t bottom_linesize,
                       uint8_t *dst, ptrdiff_t dst_linesize,
                       ptrdiff_t width, ptrdiff_t start, ptrdiff_t end,
                       struct FilterParams *param, double *values);

void ff_blend_difference_ssse3(const uint8_t *top, ptrdiff_t top_linesize,
                               const uint8_t *bottom, ptrdiff_t bottom_linesize,
                               uint8_t *dst, ptrdiff_t dst_linesize,
                               ptrdiff_t width, ptrdiff_t start, ptrdiff_t end,
                               struct FilterParams *param, double *values);

void ff_blend_negation_ssse3(const uint8_t *top, ptrdiff_t top_linesize,
                             const uint8_t *bottom, ptrdiff_t bottom_linesize,
                             uint8_t *dst, ptrdiff_t dst_linesize,
                             ptrdiff_t width, ptrdiff_t start, ptrdiff_t end,
                             struct FilterParams *param, double *values);

av_cold void ff_blend_init_x86(FilterParams *param, int is_16bit)
{
    int cpu_flags = av_get_cpu_flags();

    if (ARCH_X86_64 && EXTERNAL_SSE2(cpu_flags) && param->opacity == 1 && !is_16bit) {
        switch (param->mode) {
        case BLEND_ADDITION: param->blend = ff_blend_addition_sse2; break;
        case BLEND_ADDITION128: param->blend = ff_blend_addition128_sse2; break;
        case BLEND_AND:      param->blend = ff_blend_and_sse2;      break;
        case BLEND_AVERAGE:  param->blend = ff_blend_average_sse2;  break;
        case BLEND_DARKEN:   param->blend = ff_blend_darken_sse2;   break;
        case BLEND_DIFFERENCE128: param->blend = ff_blend_difference128_sse2; break;
        case BLEND_HARDMIX:  param->blend = ff_blend_hardmix_sse2;  break;
        case BLEND_LIGHTEN:  param->blend = ff_blend_lighten_sse2;  break;
        case BLEND_OR:       param->blend = ff_blend_or_sse2;       break;
        case BLEND_PHOENIX:  param->blend = ff_blend_phoenix_sse2;  break;
        case BLEND_SUBTRACT: param->blend = ff_blend_subtract_sse2; break;
        case BLEND_XOR:      param->blend = ff_blend_xor_sse2;      break;
        }
    }
    if (ARCH_X86_64 && EXTERNAL_SSSE3(cpu_flags) && param->opacity == 1 && !is_16bit) {
        switch (param->mode) {
        case BLEND_DIFFERENCE: param->blend = ff_blend_difference_ssse3; break;
        case BLEND_NEGATION:   param->blend = ff_blend_negation_ssse3;   break;
        }
    }
}

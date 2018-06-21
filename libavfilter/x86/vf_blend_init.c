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

#define BLEND_FUNC(name, opt) \
void ff_blend_##name##_##opt(const uint8_t *top, ptrdiff_t top_linesize,       \
                             const uint8_t *bottom, ptrdiff_t bottom_linesize, \
                             uint8_t *dst, ptrdiff_t dst_linesize,             \
                             ptrdiff_t width, ptrdiff_t height,                \
                             struct FilterParams *param, double *values, int starty);

BLEND_FUNC(addition, sse2)
BLEND_FUNC(addition, avx2)
BLEND_FUNC(grainmerge, sse2)
BLEND_FUNC(grainmerge, avx2)
BLEND_FUNC(average, sse2)
BLEND_FUNC(average, avx2)
BLEND_FUNC(and, sse2)
BLEND_FUNC(and, avx2)
BLEND_FUNC(darken, sse2)
BLEND_FUNC(darken, avx2)
BLEND_FUNC(grainextract, sse2)
BLEND_FUNC(grainextract, avx2)
BLEND_FUNC(multiply, sse2)
BLEND_FUNC(multiply, avx2)
BLEND_FUNC(screen, sse2)
BLEND_FUNC(screen, avx2)
BLEND_FUNC(hardmix, sse2)
BLEND_FUNC(hardmix, avx2)
BLEND_FUNC(divide, sse2)
BLEND_FUNC(lighten, sse2)
BLEND_FUNC(lighten, avx2)
BLEND_FUNC(or, sse2)
BLEND_FUNC(or, avx2)
BLEND_FUNC(phoenix, sse2)
BLEND_FUNC(phoenix, avx2)
BLEND_FUNC(subtract, sse2)
BLEND_FUNC(subtract, avx2)
BLEND_FUNC(xor, sse2)
BLEND_FUNC(xor, avx2)
BLEND_FUNC(difference, sse2)
BLEND_FUNC(difference, ssse3)
BLEND_FUNC(difference, avx2)
BLEND_FUNC(extremity, sse2)
BLEND_FUNC(extremity, ssse3)
BLEND_FUNC(extremity, avx2)
BLEND_FUNC(negation, sse2)
BLEND_FUNC(negation, ssse3)
BLEND_FUNC(negation, avx2)

#if ARCH_X86_64
BLEND_FUNC(addition_16, sse2)
BLEND_FUNC(addition_16, avx2)
BLEND_FUNC(grainmerge_16, sse4)
BLEND_FUNC(grainmerge_16, avx2)
BLEND_FUNC(average_16, sse2)
BLEND_FUNC(average_16, avx2)
BLEND_FUNC(and_16, sse2)
BLEND_FUNC(and_16, avx2)
BLEND_FUNC(darken_16, sse4)
BLEND_FUNC(darken_16, avx2)
BLEND_FUNC(grainextract_16, sse4)
BLEND_FUNC(grainextract_16, avx2)
BLEND_FUNC(difference_16, sse4)
BLEND_FUNC(difference_16, avx2)
BLEND_FUNC(extremity_16, sse4)
BLEND_FUNC(extremity_16, avx2)
BLEND_FUNC(negation_16, sse4)
BLEND_FUNC(negation_16, avx2)
BLEND_FUNC(lighten_16, sse4)
BLEND_FUNC(lighten_16, avx2)
BLEND_FUNC(or_16, sse2)
BLEND_FUNC(or_16, avx2)
BLEND_FUNC(phoenix_16, sse4)
BLEND_FUNC(phoenix_16, avx2)
BLEND_FUNC(subtract_16, sse2)
BLEND_FUNC(subtract_16, avx2)
BLEND_FUNC(xor_16, sse2)
BLEND_FUNC(xor_16, avx2)
#endif /* ARCH_X86_64 */

av_cold void ff_blend_init_x86(FilterParams *param, int is_16bit)
{
    int cpu_flags = av_get_cpu_flags();

    if (!is_16bit) {
        if (EXTERNAL_SSE2(cpu_flags) && param->opacity == 1) {
            switch (param->mode) {
            case BLEND_ADDITION:     param->blend = ff_blend_addition_sse2;     break;
            case BLEND_GRAINMERGE:   param->blend = ff_blend_grainmerge_sse2;   break;
            case BLEND_AND:          param->blend = ff_blend_and_sse2;          break;
            case BLEND_AVERAGE:      param->blend = ff_blend_average_sse2;      break;
            case BLEND_DARKEN:       param->blend = ff_blend_darken_sse2;       break;
            case BLEND_GRAINEXTRACT: param->blend = ff_blend_grainextract_sse2; break;
            case BLEND_DIVIDE:       param->blend = ff_blend_divide_sse2;       break;
            case BLEND_HARDMIX:      param->blend = ff_blend_hardmix_sse2;      break;
            case BLEND_LIGHTEN:      param->blend = ff_blend_lighten_sse2;      break;
            case BLEND_MULTIPLY:     param->blend = ff_blend_multiply_sse2;     break;
            case BLEND_OR:           param->blend = ff_blend_or_sse2;           break;
            case BLEND_PHOENIX:      param->blend = ff_blend_phoenix_sse2;      break;
            case BLEND_SCREEN:       param->blend = ff_blend_screen_sse2;       break;
            case BLEND_SUBTRACT:     param->blend = ff_blend_subtract_sse2;     break;
            case BLEND_XOR:          param->blend = ff_blend_xor_sse2;          break;
            case BLEND_DIFFERENCE:   param->blend = ff_blend_difference_sse2;   break;
            case BLEND_EXTREMITY:    param->blend = ff_blend_extremity_sse2;    break;
            case BLEND_NEGATION:     param->blend = ff_blend_negation_sse2;     break;
            }
        }
        if (EXTERNAL_SSSE3(cpu_flags) && param->opacity == 1) {
            switch (param->mode) {
            case BLEND_DIFFERENCE: param->blend = ff_blend_difference_ssse3; break;
            case BLEND_EXTREMITY:  param->blend = ff_blend_extremity_ssse3;  break;
            case BLEND_NEGATION:   param->blend = ff_blend_negation_ssse3;   break;
            }
        }

        if (EXTERNAL_AVX2_FAST(cpu_flags) && param->opacity == 1) {
            switch (param->mode) {
            case BLEND_ADDITION:     param->blend = ff_blend_addition_avx2;     break;
            case BLEND_GRAINMERGE:   param->blend = ff_blend_grainmerge_avx2;   break;
            case BLEND_AND:          param->blend = ff_blend_and_avx2;          break;
            case BLEND_AVERAGE:      param->blend = ff_blend_average_avx2;      break;
            case BLEND_DARKEN:       param->blend = ff_blend_darken_avx2;       break;
            case BLEND_GRAINEXTRACT: param->blend = ff_blend_grainextract_avx2; break;
            case BLEND_HARDMIX:      param->blend = ff_blend_hardmix_avx2;      break;
            case BLEND_LIGHTEN:      param->blend = ff_blend_lighten_avx2;      break;
            case BLEND_MULTIPLY:     param->blend = ff_blend_multiply_avx2;     break;
            case BLEND_OR:           param->blend = ff_blend_or_avx2;           break;
            case BLEND_PHOENIX:      param->blend = ff_blend_phoenix_avx2;      break;
            case BLEND_SCREEN:       param->blend = ff_blend_screen_avx2;       break;
            case BLEND_SUBTRACT:     param->blend = ff_blend_subtract_avx2;     break;
            case BLEND_XOR:          param->blend = ff_blend_xor_avx2;          break;
            case BLEND_DIFFERENCE:   param->blend = ff_blend_difference_avx2;   break;
            case BLEND_EXTREMITY:    param->blend = ff_blend_extremity_avx2;    break;
            case BLEND_NEGATION:     param->blend = ff_blend_negation_avx2;     break;
            }
        }
    } else { /* is_16_bit */
#if ARCH_X86_64
        if (EXTERNAL_SSE2(cpu_flags) && param->opacity == 1) {
            switch (param->mode) {
            case BLEND_ADDITION: param->blend = ff_blend_addition_16_sse2; break;
            case BLEND_AND:      param->blend = ff_blend_and_16_sse2;      break;
            case BLEND_AVERAGE:  param->blend = ff_blend_average_16_sse2;  break;
            case BLEND_OR:       param->blend = ff_blend_or_16_sse2;       break;
            case BLEND_SUBTRACT: param->blend = ff_blend_subtract_16_sse2; break;
            case BLEND_XOR:      param->blend = ff_blend_xor_16_sse2;      break;
            }
        }
        if (EXTERNAL_SSE4(cpu_flags) && param->opacity == 1) {
            switch (param->mode) {
            case BLEND_GRAINMERGE: param->blend = ff_blend_grainmerge_16_sse4; break;
            case BLEND_DARKEN:   param->blend = ff_blend_darken_16_sse4;     break;
            case BLEND_GRAINEXTRACT: param->blend = ff_blend_grainextract_16_sse4; break;
            case BLEND_DIFFERENCE: param->blend = ff_blend_difference_16_sse4; break;
            case BLEND_EXTREMITY:  param->blend = ff_blend_extremity_16_sse4;    break;
            case BLEND_NEGATION:  param->blend = ff_blend_negation_16_sse4;     break;
            case BLEND_LIGHTEN:  param->blend = ff_blend_lighten_16_sse4;    break;
            case BLEND_PHOENIX:  param->blend = ff_blend_phoenix_16_sse4;    break;
            }
        }
        if (EXTERNAL_AVX2_FAST(cpu_flags) && param->opacity == 1) {
            switch (param->mode) {
            case BLEND_ADDITION: param->blend = ff_blend_addition_16_avx2; break;
            case BLEND_GRAINMERGE: param->blend = ff_blend_grainmerge_16_avx2;   break;
            case BLEND_AND:      param->blend = ff_blend_and_16_avx2;      break;
            case BLEND_AVERAGE:  param->blend = ff_blend_average_16_avx2;  break;
            case BLEND_DARKEN:   param->blend = ff_blend_darken_16_avx2;   break;
            case BLEND_GRAINEXTRACT: param->blend = ff_blend_grainextract_16_avx2; break;
            case BLEND_DIFFERENCE: param->blend = ff_blend_difference_16_avx2; break;
            case BLEND_EXTREMITY:  param->blend = ff_blend_extremity_16_avx2;    break;
            case BLEND_NEGATION:  param->blend = ff_blend_negation_16_avx2;     break;
            case BLEND_LIGHTEN:  param->blend = ff_blend_lighten_16_avx2;  break;
            case BLEND_OR:       param->blend = ff_blend_or_16_avx2;       break;
            case BLEND_PHOENIX:  param->blend = ff_blend_phoenix_16_avx2;  break;
            case BLEND_SUBTRACT: param->blend = ff_blend_subtract_16_avx2; break;
            case BLEND_XOR:      param->blend = ff_blend_xor_16_avx2;      break;
            }
        }
#endif /* ARCH_X86_64 */
    }
}

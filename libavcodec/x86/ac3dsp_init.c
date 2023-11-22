/*
 * x86-optimized AC-3 DSP functions
 * Copyright (c) 2011 Justin Ruggles
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
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/ac3dsp.h"

void ff_ac3_exponent_min_sse2  (uint8_t *exp, int num_reuse_blocks, int nb_coefs);

void ff_float_to_fixed24_sse2 (int32_t *dst, const float *src, size_t len);
void ff_float_to_fixed24_avx  (int32_t *dst, const float *src, size_t len);

int ff_ac3_compute_mantissa_size_sse2(uint16_t mant_cnt[6][16]);

void ff_ac3_extract_exponents_sse2 (uint8_t *exp, int32_t *coef, int nb_coefs);
void ff_ac3_extract_exponents_ssse3(uint8_t *exp, int32_t *coef, int nb_coefs);

av_cold void ff_ac3dsp_init_x86(AC3DSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->ac3_exponent_min = ff_ac3_exponent_min_sse2;
        c->float_to_fixed24 = ff_float_to_fixed24_sse2;
        c->compute_mantissa_size = ff_ac3_compute_mantissa_size_sse2;
        c->extract_exponents = ff_ac3_extract_exponents_sse2;
    }

    if (EXTERNAL_SSSE3(cpu_flags)) {
        if (!(cpu_flags & AV_CPU_FLAG_ATOM))
            c->extract_exponents = ff_ac3_extract_exponents_ssse3;
    }
    if (EXTERNAL_AVX_FAST(cpu_flags)) {
        c->float_to_fixed24 = ff_float_to_fixed24_avx;
    }
}

#define DOWNMIX_FUNC_OPT(ch, opt)                                       \
void ff_ac3_downmix_ ## ch ## _to_1_ ## opt(float **samples,            \
                                            float **matrix, int len);   \
void ff_ac3_downmix_ ## ch ## _to_2_ ## opt(float **samples,            \
                                            float **matrix, int len);

#define DOWNMIX_FUNCS(opt)   \
    DOWNMIX_FUNC_OPT(3, opt) \
    DOWNMIX_FUNC_OPT(4, opt) \
    DOWNMIX_FUNC_OPT(5, opt) \
    DOWNMIX_FUNC_OPT(6, opt)

DOWNMIX_FUNCS(sse)
DOWNMIX_FUNCS(avx)
DOWNMIX_FUNCS(fma3)

void ff_ac3dsp_set_downmix_x86(AC3DSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

#define SET_DOWNMIX(ch, suf, SUF)                                       \
    if (ch == c->in_channels) {                                         \
        if (EXTERNAL_ ## SUF (cpu_flags)) {                             \
            if (c->out_channels == 1)                                   \
                c->downmix = ff_ac3_downmix_ ## ch ## _to_1_ ## suf;    \
            else                                                        \
                c->downmix = ff_ac3_downmix_ ## ch ## _to_2_ ## suf;    \
        }                                                               \
    }

#define SET_DOWNMIX_ALL(suf, SUF)                   \
    SET_DOWNMIX(3, suf, SUF)                        \
    SET_DOWNMIX(4, suf, SUF)                        \
    SET_DOWNMIX(5, suf, SUF)                        \
    SET_DOWNMIX(6, suf, SUF)

    SET_DOWNMIX_ALL(sse,  SSE)
    if (!(cpu_flags & AV_CPU_FLAG_AVXSLOW)) {
        SET_DOWNMIX_ALL(avx,  AVX)
        SET_DOWNMIX_ALL(fma3, FMA3)
    }
}

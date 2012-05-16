/*
 * x86-optimized AC-3 DSP utils
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

#include "libavutil/x86_cpu.h"
#include "dsputil_mmx.h"
#include "libavcodec/ac3dsp.h"

extern void ff_ac3_exponent_min_mmx   (uint8_t *exp, int num_reuse_blocks, int nb_coefs);
extern void ff_ac3_exponent_min_mmxext(uint8_t *exp, int num_reuse_blocks, int nb_coefs);
extern void ff_ac3_exponent_min_sse2  (uint8_t *exp, int num_reuse_blocks, int nb_coefs);

extern int ff_ac3_max_msb_abs_int16_mmx  (const int16_t *src, int len);
extern int ff_ac3_max_msb_abs_int16_mmx2 (const int16_t *src, int len);
extern int ff_ac3_max_msb_abs_int16_sse2 (const int16_t *src, int len);
extern int ff_ac3_max_msb_abs_int16_ssse3(const int16_t *src, int len);

extern void ff_ac3_lshift_int16_mmx (int16_t *src, unsigned int len, unsigned int shift);
extern void ff_ac3_lshift_int16_sse2(int16_t *src, unsigned int len, unsigned int shift);

extern void ff_ac3_rshift_int32_mmx (int32_t *src, unsigned int len, unsigned int shift);
extern void ff_ac3_rshift_int32_sse2(int32_t *src, unsigned int len, unsigned int shift);

extern void ff_float_to_fixed24_3dnow(int32_t *dst, const float *src, unsigned int len);
extern void ff_float_to_fixed24_sse  (int32_t *dst, const float *src, unsigned int len);
extern void ff_float_to_fixed24_sse2 (int32_t *dst, const float *src, unsigned int len);

extern int ff_ac3_compute_mantissa_size_sse2(uint16_t mant_cnt[6][16]);

extern void ff_ac3_extract_exponents_3dnow(uint8_t *exp, int32_t *coef, int nb_coefs);
extern void ff_ac3_extract_exponents_sse2 (uint8_t *exp, int32_t *coef, int nb_coefs);
extern void ff_ac3_extract_exponents_ssse3(uint8_t *exp, int32_t *coef, int nb_coefs);

av_cold void ff_ac3dsp_init_x86(AC3DSPContext *c, int bit_exact)
{
#if HAVE_YASM
    int mm_flags = av_get_cpu_flags();

    if (mm_flags & AV_CPU_FLAG_MMX) {
        c->ac3_exponent_min = ff_ac3_exponent_min_mmx;
        c->ac3_max_msb_abs_int16 = ff_ac3_max_msb_abs_int16_mmx;
        c->ac3_lshift_int16 = ff_ac3_lshift_int16_mmx;
        c->ac3_rshift_int32 = ff_ac3_rshift_int32_mmx;
    }
    if (mm_flags & AV_CPU_FLAG_3DNOW && HAVE_AMD3DNOW) {
        c->extract_exponents = ff_ac3_extract_exponents_3dnow;
        if (!bit_exact) {
            c->float_to_fixed24 = ff_float_to_fixed24_3dnow;
        }
    }
    if (mm_flags & AV_CPU_FLAG_MMX2 && HAVE_MMX2) {
        c->ac3_exponent_min = ff_ac3_exponent_min_mmxext;
        c->ac3_max_msb_abs_int16 = ff_ac3_max_msb_abs_int16_mmx2;
    }
    if (mm_flags & AV_CPU_FLAG_SSE && HAVE_SSE) {
        c->float_to_fixed24 = ff_float_to_fixed24_sse;
    }
    if (mm_flags & AV_CPU_FLAG_SSE2 && HAVE_SSE) {
        c->ac3_exponent_min = ff_ac3_exponent_min_sse2;
        c->ac3_max_msb_abs_int16 = ff_ac3_max_msb_abs_int16_sse2;
        c->float_to_fixed24 = ff_float_to_fixed24_sse2;
        c->compute_mantissa_size = ff_ac3_compute_mantissa_size_sse2;
        c->extract_exponents = ff_ac3_extract_exponents_sse2;
        if (!(mm_flags & AV_CPU_FLAG_SSE2SLOW)) {
            c->ac3_lshift_int16 = ff_ac3_lshift_int16_sse2;
            c->ac3_rshift_int32 = ff_ac3_rshift_int32_sse2;
        }
    }
    if (mm_flags & AV_CPU_FLAG_SSSE3 && HAVE_SSSE3) {
        c->ac3_max_msb_abs_int16 = ff_ac3_max_msb_abs_int16_ssse3;
        if (!(mm_flags & AV_CPU_FLAG_ATOM)) {
            c->extract_exponents = ff_ac3_extract_exponents_ssse3;
        }
    }
#endif
}

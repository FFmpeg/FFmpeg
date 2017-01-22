/*
 * Copyright (c) 2016 Ronald S. Bultje <rsbultje@gmail.com>
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

#include "libavutil/x86/cpu.h"

#include "libavfilter/colorspacedsp.h"

#define decl_yuv2yuv_fn(t) \
void ff_yuv2yuv_##t##_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_out_stride[3], \
                           uint8_t *yuv_in[3], const ptrdiff_t yuv_in_stride[3], \
                           int w, int h, const int16_t yuv2yuv_coeffs[3][3][8], \
                           const int16_t yuv_offset[2][8])

#define decl_yuv2yuv_fns(ss) \
decl_yuv2yuv_fn(ss##p8to8); \
decl_yuv2yuv_fn(ss##p10to8); \
decl_yuv2yuv_fn(ss##p12to8); \
decl_yuv2yuv_fn(ss##p8to10); \
decl_yuv2yuv_fn(ss##p10to10); \
decl_yuv2yuv_fn(ss##p12to10); \
decl_yuv2yuv_fn(ss##p8to12); \
decl_yuv2yuv_fn(ss##p10to12); \
decl_yuv2yuv_fn(ss##p12to12)

decl_yuv2yuv_fns(420);
decl_yuv2yuv_fns(422);
decl_yuv2yuv_fns(444);

#define decl_yuv2rgb_fn(t) \
void ff_yuv2rgb_##t##_sse2(int16_t *rgb_out[3], ptrdiff_t rgb_stride, \
                           uint8_t *yuv_in[3], const ptrdiff_t yuv_stride[3], \
                           int w, int h, const int16_t coeff[3][3][8], \
                           const int16_t yuv_offset[8])

#define decl_yuv2rgb_fns(ss) \
decl_yuv2rgb_fn(ss##p8); \
decl_yuv2rgb_fn(ss##p10); \
decl_yuv2rgb_fn(ss##p12)

decl_yuv2rgb_fns(420);
decl_yuv2rgb_fns(422);
decl_yuv2rgb_fns(444);

#define decl_rgb2yuv_fn(t) \
void ff_rgb2yuv_##t##_sse2(uint8_t *yuv_out[3], const ptrdiff_t yuv_stride[3], \
                           int16_t *rgb_in[3], ptrdiff_t rgb_stride, \
                           int w, int h, const int16_t coeff[3][3][8], \
                           const int16_t yuv_offset[8])

#define decl_rgb2yuv_fns(ss) \
decl_rgb2yuv_fn(ss##p8); \
decl_rgb2yuv_fn(ss##p10); \
decl_rgb2yuv_fn(ss##p12)

decl_rgb2yuv_fns(420);
decl_rgb2yuv_fns(422);
decl_rgb2yuv_fns(444);

void ff_multiply3x3_sse2(int16_t *data[3], ptrdiff_t stride, int w, int h,
                         const int16_t coeff[3][3][8]);

void ff_colorspacedsp_x86_init(ColorSpaceDSPContext *dsp)
{
    int cpu_flags = av_get_cpu_flags();

    if (ARCH_X86_64 && EXTERNAL_SSE2(cpu_flags)) {
#define assign_yuv2yuv_fns(ss) \
        dsp->yuv2yuv[BPP_8 ][BPP_8 ][SS_##ss] = ff_yuv2yuv_##ss##p8to8_sse2; \
        dsp->yuv2yuv[BPP_8 ][BPP_10][SS_##ss] = ff_yuv2yuv_##ss##p8to10_sse2; \
        dsp->yuv2yuv[BPP_8 ][BPP_12][SS_##ss] = ff_yuv2yuv_##ss##p8to12_sse2; \
        dsp->yuv2yuv[BPP_10][BPP_8 ][SS_##ss] = ff_yuv2yuv_##ss##p10to8_sse2; \
        dsp->yuv2yuv[BPP_10][BPP_10][SS_##ss] = ff_yuv2yuv_##ss##p10to10_sse2; \
        dsp->yuv2yuv[BPP_10][BPP_12][SS_##ss] = ff_yuv2yuv_##ss##p10to12_sse2; \
        dsp->yuv2yuv[BPP_12][BPP_8 ][SS_##ss] = ff_yuv2yuv_##ss##p12to8_sse2; \
        dsp->yuv2yuv[BPP_12][BPP_10][SS_##ss] = ff_yuv2yuv_##ss##p12to10_sse2; \
        dsp->yuv2yuv[BPP_12][BPP_12][SS_##ss] = ff_yuv2yuv_##ss##p12to12_sse2

        assign_yuv2yuv_fns(420);
        assign_yuv2yuv_fns(422);
        assign_yuv2yuv_fns(444);

#define assign_yuv2rgb_fns(ss) \
        dsp->yuv2rgb[BPP_8 ][SS_##ss] = ff_yuv2rgb_##ss##p8_sse2; \
        dsp->yuv2rgb[BPP_10][SS_##ss] = ff_yuv2rgb_##ss##p10_sse2; \
        dsp->yuv2rgb[BPP_12][SS_##ss] = ff_yuv2rgb_##ss##p12_sse2

        assign_yuv2rgb_fns(420);
        assign_yuv2rgb_fns(422);
        assign_yuv2rgb_fns(444);

#define assign_rgb2yuv_fns(ss) \
        dsp->rgb2yuv[BPP_8 ][SS_##ss] = ff_rgb2yuv_##ss##p8_sse2; \
        dsp->rgb2yuv[BPP_10][SS_##ss] = ff_rgb2yuv_##ss##p10_sse2; \
        dsp->rgb2yuv[BPP_12][SS_##ss] = ff_rgb2yuv_##ss##p12_sse2

        assign_rgb2yuv_fns(420);
        assign_rgb2yuv_fns(422);
        assign_rgb2yuv_fns(444);

        dsp->multiply3x3 = ff_multiply3x3_sse2;
    }
}

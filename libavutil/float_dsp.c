/*
 * Copyright 2005 Balatoni Denes
 * Copyright 2006 Loren Merritt
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
#include "float_dsp.h"

static void vector_fmul_c(float *dst, const float *src0, const float *src1,
                          int len)
{
    int i;
    for (i = 0; i < len; i++)
        dst[i] = src0[i] * src1[i];
}

static void vector_fmac_scalar_c(float *dst, const float *src, float mul,
                                 int len)
{
    int i;
    for (i = 0; i < len; i++)
        dst[i] += src[i] * mul;
}

static void vector_fmul_scalar_c(float *dst, const float *src, float mul,
                                 int len)
{
    int i;
    for (i = 0; i < len; i++)
        dst[i] = src[i] * mul;
}

static void vector_dmul_scalar_c(double *dst, const double *src, double mul,
                                 int len)
{
    int i;
    for (i = 0; i < len; i++)
        dst[i] = src[i] * mul;
}

static void vector_fmul_window_c(float *dst, const float *src0,
                                 const float *src1, const float *win, int len)
{
    int i, j;

    dst  += len;
    win  += len;
    src0 += len;

    for (i = -len, j = len - 1; i < 0; i++, j--) {
        float s0 = src0[i];
        float s1 = src1[j];
        float wi = win[i];
        float wj = win[j];
        dst[i] = s0 * wj - s1 * wi;
        dst[j] = s0 * wi + s1 * wj;
    }
}

static void vector_fmul_add_c(float *dst, const float *src0, const float *src1,
                              const float *src2, int len){
    int i;

    for (i = 0; i < len; i++)
        dst[i] = src0[i] * src1[i] + src2[i];
}

static void vector_fmul_reverse_c(float *dst, const float *src0,
                                  const float *src1, int len)
{
    int i;

    src1 += len-1;
    for (i = 0; i < len; i++)
        dst[i] = src0[i] * src1[-i];
}

static void butterflies_float_c(float *av_restrict v1, float *av_restrict v2,
                                int len)
{
    int i;

    for (i = 0; i < len; i++) {
        float t = v1[i] - v2[i];
        v1[i] += v2[i];
        v2[i] = t;
    }
}

float avpriv_scalarproduct_float_c(const float *v1, const float *v2, int len)
{
    float p = 0.0;
    int i;

    for (i = 0; i < len; i++)
        p += v1[i] * v2[i];

    return p;
}

av_cold void avpriv_float_dsp_init(AVFloatDSPContext *fdsp, int bit_exact)
{
    fdsp->vector_fmul = vector_fmul_c;
    fdsp->vector_fmac_scalar = vector_fmac_scalar_c;
    fdsp->vector_fmul_scalar = vector_fmul_scalar_c;
    fdsp->vector_dmul_scalar = vector_dmul_scalar_c;
    fdsp->vector_fmul_window = vector_fmul_window_c;
    fdsp->vector_fmul_add = vector_fmul_add_c;
    fdsp->vector_fmul_reverse = vector_fmul_reverse_c;
    fdsp->butterflies_float = butterflies_float_c;
    fdsp->scalarproduct_float = avpriv_scalarproduct_float_c;

#if ARCH_ARM
    ff_float_dsp_init_arm(fdsp);
#elif ARCH_PPC
    ff_float_dsp_init_ppc(fdsp, bit_exact);
#elif ARCH_X86
    ff_float_dsp_init_x86(fdsp);
#elif ARCH_MIPS
    ff_float_dsp_init_mips(fdsp);
#endif
}

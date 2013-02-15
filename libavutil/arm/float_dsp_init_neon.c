/*
 * ARM NEON optimised Float DSP functions
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdint.h>

#include "libavutil/float_dsp.h"
#include "float_dsp_arm.h"

void ff_vector_fmul_neon(float *dst, const float *src0, const float *src1, int len);

void ff_vector_fmac_scalar_neon(float *dst, const float *src, float mul,
                                int len);

void ff_vector_fmul_scalar_neon(float *dst, const float *src, float mul,
                                int len);

void ff_vector_fmul_window_neon(float *dst, const float *src0,
                                const float *src1, const float *win, int len);

void ff_vector_fmul_add_neon(float *dst, const float *src0, const float *src1,
                             const float *src2, int len);

void ff_vector_fmul_reverse_neon(float *dst, const float *src0,
                                 const float *src1, int len);

void ff_butterflies_float_neon(float *v1, float *v2, int len);

float ff_scalarproduct_float_neon(const float *v1, const float *v2, int len);

void ff_float_dsp_init_neon(AVFloatDSPContext *fdsp)
{
    fdsp->vector_fmul = ff_vector_fmul_neon;
    fdsp->vector_fmac_scalar = ff_vector_fmac_scalar_neon;
    fdsp->vector_fmul_scalar = ff_vector_fmul_scalar_neon;
    fdsp->vector_fmul_window = ff_vector_fmul_window_neon;
    fdsp->vector_fmul_add    = ff_vector_fmul_add_neon;
    fdsp->vector_fmul_reverse = ff_vector_fmul_reverse_neon;
    fdsp->butterflies_float = ff_butterflies_float_neon;
    fdsp->scalarproduct_float = ff_scalarproduct_float_neon;
}

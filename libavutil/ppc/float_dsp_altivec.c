/*
 * Copyright (c) 2006 Luca Barbato <lu_zero@gentoo.org>
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

#include "util_altivec.h"
#include "float_dsp_altivec.h"

void ff_vector_fmul_altivec(float *dst, const float *src0, const float *src1,
                            int len)
{
    int i;
    vector float d0, d1, s, zero = (vector float)vec_splat_u32(0);
    for (i = 0; i < len - 7; i += 8) {
        d0 = vec_ld( 0, src0 + i);
        s  = vec_ld( 0, src1 + i);
        d1 = vec_ld(16, src0 + i);
        d0 = vec_madd(d0, s, zero);
        d1 = vec_madd(d1, vec_ld(16, src1 + i), zero);
        vec_st(d0,  0, dst + i);
        vec_st(d1, 16, dst + i);
    }
}

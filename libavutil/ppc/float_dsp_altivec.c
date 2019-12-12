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
    vec_f d0, d1, s, zero = (vec_f)vec_splat_u32(0);
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

void ff_vector_fmul_window_altivec(float *dst, const float *src0,
                                   const float *src1, const float *win, int len)
{
    vec_f zero, t0, t1, s0, s1, wi, wj;
    const vec_u8 reverse = vcprm(3, 2, 1, 0);
    int i, j;

    dst  += len;
    win  += len;
    src0 += len;

    zero = (vec_f)vec_splat_u32(0);

    for (i = -len * 4, j = len * 4 - 16; i < 0; i += 16, j -= 16) {
        s0 = vec_ld(i, src0);
        s1 = vec_ld(j, src1);
        wi = vec_ld(i, win);
        wj = vec_ld(j, win);

        s1 = vec_perm(s1, s1, reverse);
        wj = vec_perm(wj, wj, reverse);

        t0 = vec_madd(s0, wj, zero);
        t0 = vec_nmsub(s1, wi, t0);
        t1 = vec_madd(s0, wi, zero);
        t1 = vec_madd(s1, wj, t1);
        t1 = vec_perm(t1, t1, reverse);

        vec_st(t0, i, dst);
        vec_st(t1, j, dst);
    }
}

void ff_vector_fmul_add_altivec(float *dst, const float *src0,
                                const float *src1, const float *src2,
                                int len)
{
    int i;
    vec_f d, ss0, ss1, ss2, t0, t1, edges;

    for (i = 0; i < len - 3; i += 4) {
        t0 = vec_ld(0, dst + i);
        t1 = vec_ld(15, dst + i);
        ss0 = vec_ld(0, src0 + i);
        ss1 = vec_ld(0, src1 + i);
        ss2 = vec_ld(0, src2 + i);
        edges = vec_perm(t1, t0, vcprm(0, 1, 2, 3));
        d = vec_madd(ss0, ss1, ss2);
        t1 = vec_perm(d, edges, vcprm(s0,s1,s2,s3));
        t0 = vec_perm(edges, d, vcprm(s0,s1,s2,s3));
        vec_st(t1, 15, dst + i);
        vec_st(t0, 0, dst + i);
    }
}

void ff_vector_fmul_reverse_altivec(float *dst, const float *src0,
                                    const float *src1, int len)
{
    int i;
    vec_f d, s0, s1, h0, l0, s2, s3;
    vec_f zero = (vec_f)vec_splat_u32(0);

    src1 += len-4;
    for(i = 0; i < len - 7; i += 8) {
        s1 = vec_ld(0, src1 - i);              // [a,b,c,d]
        s0 = vec_ld(0, src0 + i);
        l0 = vec_mergel(s1, s1);               // [c,c,d,d]
        s3 = vec_ld(-16, src1 - i);
        h0 = vec_mergeh(s1, s1);               // [a,a,b,b]
        s2 = vec_ld(16, src0 + i);
        s1 = vec_mergeh(vec_mergel(l0, h0),    // [d,b,d,b]
                        vec_mergeh(l0, h0));   // [c,a,c,a]
        // [d,c,b,a]
        l0 = vec_mergel(s3, s3);
        d = vec_madd(s0, s1, zero);
        h0 = vec_mergeh(s3, s3);
        vec_st(d, 0, dst + i);
        s3 = vec_mergeh(vec_mergel(l0, h0),
                        vec_mergeh(l0, h0));
        d = vec_madd(s2, s3, zero);
        vec_st(d, 16, dst + i);
    }
}

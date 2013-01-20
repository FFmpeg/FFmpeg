/*
 * Copyright (c) 2008 Siarhei Siamashka <ssvb@users.sourceforge.net>
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

#include "config.h"
#include "asm.S"

/**
 * Assume that len is a positive number and is multiple of 8
 */
@ void ff_vector_fmul_vfp(float *dst, const float *src0, const float *src1, int len)
function ff_vector_fmul_vfp, export=1
        vpush           {d8-d15}
        fmrx            r12, fpscr
        orr             r12, r12, #(3 << 16) /* set vector size to 4 */
        fmxr            fpscr, r12

        vldmia          r1!, {s0-s3}
        vldmia          r2!, {s8-s11}
        vldmia          r1!, {s4-s7}
        vldmia          r2!, {s12-s15}
        vmul.f32        s8,  s0,  s8
1:
        subs            r3,  r3,  #16
        vmul.f32        s12, s4,  s12
        itttt           ge
        vldmiage        r1!, {s16-s19}
        vldmiage        r2!, {s24-s27}
        vldmiage        r1!, {s20-s23}
        vldmiage        r2!, {s28-s31}
        it              ge
        vmulge.f32      s24, s16, s24
        vstmia          r0!, {s8-s11}
        vstmia          r0!, {s12-s15}
        it              ge
        vmulge.f32      s28, s20, s28
        itttt           gt
        vldmiagt        r1!, {s0-s3}
        vldmiagt        r2!, {s8-s11}
        vldmiagt        r1!, {s4-s7}
        vldmiagt        r2!, {s12-s15}
        ittt            ge
        vmulge.f32      s8,  s0,  s8
        vstmiage        r0!, {s24-s27}
        vstmiage        r0!, {s28-s31}
        bgt             1b

        bic             r12, r12, #(7 << 16) /* set vector size back to 1 */
        fmxr            fpscr, r12
        vpop            {d8-d15}
        bx              lr
endfunc

/**
 * ARM VFP optimized implementation of 'vector_fmul_reverse_c' function.
 * Assume that len is a positive number and is multiple of 8
 */
@ void ff_vector_fmul_reverse_vfp(float *dst, const float *src0,
@                                 const float *src1, int len)
function ff_vector_fmul_reverse_vfp, export=1
        vpush           {d8-d15}
        add             r2,  r2,  r3, lsl #2
        vldmdb          r2!, {s0-s3}
        vldmia          r1!, {s8-s11}
        vldmdb          r2!, {s4-s7}
        vldmia          r1!, {s12-s15}
        vmul.f32        s8,  s3,  s8
        vmul.f32        s9,  s2,  s9
        vmul.f32        s10, s1,  s10
        vmul.f32        s11, s0,  s11
1:
        subs            r3,  r3,  #16
        it              ge
        vldmdbge        r2!, {s16-s19}
        vmul.f32        s12, s7,  s12
        it              ge
        vldmiage        r1!, {s24-s27}
        vmul.f32        s13, s6,  s13
        it              ge
        vldmdbge        r2!, {s20-s23}
        vmul.f32        s14, s5,  s14
        it              ge
        vldmiage        r1!, {s28-s31}
        vmul.f32        s15, s4,  s15
        it              ge
        vmulge.f32      s24, s19, s24
        it              gt
        vldmdbgt        r2!, {s0-s3}
        it              ge
        vmulge.f32      s25, s18, s25
        vstmia          r0!, {s8-s13}
        it              ge
        vmulge.f32      s26, s17, s26
        it              gt
        vldmiagt        r1!, {s8-s11}
        itt             ge
        vmulge.f32      s27, s16, s27
        vmulge.f32      s28, s23, s28
        it              gt
        vldmdbgt        r2!, {s4-s7}
        it              ge
        vmulge.f32      s29, s22, s29
        vstmia          r0!, {s14-s15}
        ittt            ge
        vmulge.f32      s30, s21, s30
        vmulge.f32      s31, s20, s31
        vmulge.f32      s8,  s3,  s8
        it              gt
        vldmiagt        r1!, {s12-s15}
        itttt           ge
        vmulge.f32      s9,  s2,  s9
        vmulge.f32      s10, s1,  s10
        vstmiage        r0!, {s24-s27}
        vmulge.f32      s11, s0,  s11
        it              ge
        vstmiage        r0!, {s28-s31}
        bgt             1b

        vpop            {d8-d15}
        bx              lr
endfunc

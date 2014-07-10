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
 * ARM VFP implementation of 'vector_fmul_window_c' function
 * Assume that len is a positive non-zero number
 */
@ void ff_vector_fmul_window_vfp(float *dst, const float *src0,
@                                const float *src1, const float *win, int len)
function ff_vector_fmul_window_vfp, export=1
DST0    .req    a1
SRC0    .req    a2
SRC1    .req    a3
WIN0    .req    a4
LEN     .req    v1
DST1    .req    v2
WIN1    .req    v3
OLDFPSCR .req   ip

        push    {v1-v3,lr}
        ldr     LEN, [sp, #4*4+0]
        vpush   {s16-s31}
        fmrx    OLDFPSCR, FPSCR
        add     DST1, DST0, LEN, lsl #3
        add     SRC1, SRC1, LEN, lsl #2
        add     WIN1, WIN0, LEN, lsl #3

        tst     LEN, #7
        beq     4f                          @ common case: len is a multiple of 8

        ldr     lr, =0x03000000             @ RunFast mode, scalar mode
        fmxr    FPSCR, lr

        tst     LEN, #1
        beq     1f
        vldmdb  WIN1!, {s0}
        vldmia  SRC0!, {s8}
        vldmia  WIN0!, {s16}
        vmul.f  s24, s0, s8
        vldmdb  SRC1!, {s20}
        vmul.f  s8, s16, s8
        vmls.f  s24, s16, s20
        vmla.f  s8, s0, s20
        vstmia  DST0!, {s24}
        vstmdb  DST1!, {s8}
1:
        tst     LEN, #2
        beq     2f
        vldmdb  WIN1!, {s0}
        vldmdb  WIN1!, {s1}
        vldmia  SRC0!, {s8-s9}
        vldmia  WIN0!, {s16-s17}
        vmul.f  s24, s0, s8
        vmul.f  s25, s1, s9
        vldmdb  SRC1!, {s20}
        vldmdb  SRC1!, {s21}
        vmul.f  s8, s16, s8
        vmul.f  s9, s17, s9
        vmls.f  s24, s16, s20
        vmls.f  s25, s17, s21
        vmla.f  s8, s0, s20
        vmla.f  s9, s1, s21
        vstmia  DST0!, {s24-s25}
        vstmdb  DST1!, {s8}
        vstmdb  DST1!, {s9}
2:
        tst     LEN, #4
        beq     3f
        vldmdb  WIN1!, {s0}
        vldmdb  WIN1!, {s1}
        vldmdb  WIN1!, {s2}
        vldmdb  WIN1!, {s3}
        vldmia  SRC0!, {s8-s11}
        vldmia  WIN0!, {s16-s19}
        vmul.f  s24, s0, s8
        vmul.f  s25, s1, s9
        vmul.f  s26, s2, s10
        vmul.f  s27, s3, s11
        vldmdb  SRC1!, {s20}
        vldmdb  SRC1!, {s21}
        vldmdb  SRC1!, {s22}
        vldmdb  SRC1!, {s23}
        vmul.f  s8, s16, s8
        vmul.f  s9, s17, s9
        vmul.f  s10, s18, s10
        vmul.f  s11, s19, s11
        vmls.f  s24, s16, s20
        vmls.f  s25, s17, s21
        vmls.f  s26, s18, s22
        vmls.f  s27, s19, s23
        vmla.f  s8, s0, s20
        vmla.f  s9, s1, s21
        vmla.f  s10, s2, s22
        vmla.f  s11, s3, s23
        vstmia  DST0!, {s24-s27}
        vstmdb  DST1!, {s8}
        vstmdb  DST1!, {s9}
        vstmdb  DST1!, {s10}
        vstmdb  DST1!, {s11}
3:
        bics    LEN, LEN, #7
        beq     7f
4:
        ldr     lr, =0x03030000             @ RunFast mode, short vectors of length 4, stride 1
        fmxr    FPSCR, lr

        vldmdb  WIN1!, {s0}
        vldmdb  WIN1!, {s1}
        vldmdb  WIN1!, {s2}
        vldmdb  WIN1!, {s3}
        vldmia  SRC0!, {s8-s11}
        vldmia  WIN0!, {s16-s19}
        vmul.f  s24, s0, s8                     @ vector * vector
        vldmdb  SRC1!, {s20}
        vldmdb  SRC1!, {s21}
        vldmdb  SRC1!, {s22}
        vldmdb  SRC1!, {s23}
        vmul.f  s8, s16, s8                     @ vector * vector
        vmls.f  s24, s16, s20                   @ vector * vector
            vldmdb  WIN1!, {s4}
            vldmdb  WIN1!, {s5}
            vldmdb  WIN1!, {s6}
            vldmdb  WIN1!, {s7}
            vldmia  SRC0!, {s12-s13}
        vmla.f  s8, s0, s20                     @ vector * vector
            vldmia  SRC0!, {s14-s15}
        subs    LEN, LEN, #8
        beq     6f
5:          vldmia  WIN0!, {s20-s23}
            vmul.f  s28, s4, s12                @ vector * vector
        vstmia  DST0!, {s24-s25}
            vldmdb  SRC1!, {s16}
            vldmdb  SRC1!, {s17}
            vldmdb  SRC1!, {s18}
            vldmdb  SRC1!, {s19}
            vmul.f  s12, s20, s12               @ vector * vector
        vstmia  DST0!, {s26-s27}
        vstmdb  DST1!, {s8}
        vstmdb  DST1!, {s9}
        vstmdb  DST1!, {s10}
        vstmdb  DST1!, {s11}
            vmls.f  s28, s20, s16               @ vector * vector
                vldmdb  WIN1!, {s0}
                vldmdb  WIN1!, {s1}
                vldmdb  WIN1!, {s2}
                vldmdb  WIN1!, {s3}
                vldmia  SRC0!, {s8-s9}
            vmla.f  s12, s4, s16                @ vector * vector
                vldmia  SRC0!, {s10-s11}
        subs    LEN, LEN, #8
                vldmia  WIN0!, {s16-s19}
                vmul.f  s24, s0, s8             @ vector * vector
            vstmia  DST0!, {s28-s29}
                vldmdb  SRC1!, {s20}
                vldmdb  SRC1!, {s21}
                vldmdb  SRC1!, {s22}
                vldmdb  SRC1!, {s23}
                vmul.f  s8, s16, s8             @ vector * vector
            vstmia  DST0!, {s30-s31}
            vstmdb  DST1!, {s12}
            vstmdb  DST1!, {s13}
            vstmdb  DST1!, {s14}
            vstmdb  DST1!, {s15}
                vmls.f  s24, s16, s20           @ vector * vector
                    vldmdb  WIN1!, {s4}
                    vldmdb  WIN1!, {s5}
                    vldmdb  WIN1!, {s6}
                    vldmdb  WIN1!, {s7}
                    vldmia  SRC0!, {s12-s13}
                vmla.f  s8, s0, s20             @ vector * vector
                    vldmia  SRC0!, {s14-s15}
        bne     5b
6:                  vldmia  WIN0!, {s20-s23}
                    vmul.f  s28, s4, s12        @ vector * vector
                vstmia  DST0!, {s24-s25}
                    vldmdb  SRC1!, {s16}
                    vldmdb  SRC1!, {s17}
                    vldmdb  SRC1!, {s18}
                    vldmdb  SRC1!, {s19}
                    vmul.f  s12, s20, s12       @ vector * vector
                vstmia  DST0!, {s26-s27}
                vstmdb  DST1!, {s8}
                vstmdb  DST1!, {s9}
                vstmdb  DST1!, {s10}
                vstmdb  DST1!, {s11}
                    vmls.f  s28, s20, s16       @ vector * vector
                    vmla.f  s12, s4, s16        @ vector * vector
                    vstmia  DST0!, {s28-s31}
                    vstmdb  DST1!, {s12}
                    vstmdb  DST1!, {s13}
                    vstmdb  DST1!, {s14}
                    vstmdb  DST1!, {s15}
7:
        fmxr    FPSCR, OLDFPSCR
        vpop    {s16-s31}
        pop     {v1-v3,pc}

        .unreq  DST0
        .unreq  SRC0
        .unreq  SRC1
        .unreq  WIN0
        .unreq  LEN
        .unreq  OLDFPSCR
        .unreq  DST1
        .unreq  WIN1
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

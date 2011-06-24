/*
 * Copyright (c) 2008 Siarhei Siamashka <ssvb@users.sourceforge.net>
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
#include "asm.S"

/**
 * ARM VFP optimized float to int16 conversion.
 * Assume that len is a positive number and is multiple of 8, destination
 * buffer is at least 4 bytes aligned (8 bytes alignment is better for
 * performance), little endian byte sex
 */
@ void ff_float_to_int16_vfp(int16_t *dst, const float *src, int len)
function ff_float_to_int16_vfp, export=1
        push            {r4-r8,lr}
        vpush           {d8-d11}
        vldmia          r1!, {s16-s23}
        vcvt.s32.f32    s0,  s16
        vcvt.s32.f32    s1,  s17
        vcvt.s32.f32    s2,  s18
        vcvt.s32.f32    s3,  s19
        vcvt.s32.f32    s4,  s20
        vcvt.s32.f32    s5,  s21
        vcvt.s32.f32    s6,  s22
        vcvt.s32.f32    s7,  s23
1:
        subs            r2,  r2,  #8
        vmov            r3,  r4,  s0, s1
        vmov            r5,  r6,  s2, s3
        vmov            r7,  r8,  s4, s5
        vmov            ip,  lr,  s6, s7
        it              gt
        vldmiagt        r1!, {s16-s23}
        ssat            r4,  #16, r4
        ssat            r3,  #16, r3
        ssat            r6,  #16, r6
        ssat            r5,  #16, r5
        pkhbt           r3,  r3,  r4, lsl #16
        pkhbt           r4,  r5,  r6, lsl #16
        itttt           gt
        vcvtgt.s32.f32  s0,  s16
        vcvtgt.s32.f32  s1,  s17
        vcvtgt.s32.f32  s2,  s18
        vcvtgt.s32.f32  s3,  s19
        itttt           gt
        vcvtgt.s32.f32  s4,  s20
        vcvtgt.s32.f32  s5,  s21
        vcvtgt.s32.f32  s6,  s22
        vcvtgt.s32.f32  s7,  s23
        ssat            r8,  #16, r8
        ssat            r7,  #16, r7
        ssat            lr,  #16, lr
        ssat            ip,  #16, ip
        pkhbt           r5,  r7,  r8, lsl #16
        pkhbt           r6,  ip,  lr, lsl #16
        stmia           r0!, {r3-r6}
        bgt             1b

        vpop            {d8-d11}
        pop             {r4-r8,pc}
endfunc

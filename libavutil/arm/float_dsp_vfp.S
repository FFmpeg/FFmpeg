/*
 * Copyright (c) 2008 Siarhei Siamashka <ssvb@users.sourceforge.net>
 *
 * This file is part of FFmpeg
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

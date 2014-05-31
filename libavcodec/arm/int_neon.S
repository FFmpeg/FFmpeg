/*
 * ARM NEON optimised integer operations
 * Copyright (c) 2009 Konstantin Shishkov
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

#include "libavutil/arm/asm.S"

function ff_scalarproduct_int16_neon, export=1
        vmov.i16        q0,  #0
        vmov.i16        q1,  #0
        vmov.i16        q2,  #0
        vmov.i16        q3,  #0
1:      vld1.16         {d16-d17}, [r0]!
        vld1.16         {d20-d21}, [r1,:128]!
        vmlal.s16       q0,  d16,  d20
        vld1.16         {d18-d19}, [r0]!
        vmlal.s16       q1,  d17,  d21
        vld1.16         {d22-d23}, [r1,:128]!
        vmlal.s16       q2,  d18,  d22
        vmlal.s16       q3,  d19,  d23
        subs            r2,  r2,   #16
        bgt             1b

        vpadd.s32       d16, d0,   d1
        vpadd.s32       d17, d2,   d3
        vpadd.s32       d18, d4,   d5
        vpadd.s32       d19, d6,   d7
        vpadd.s32       d0,  d16,  d17
        vpadd.s32       d1,  d18,  d19
        vpadd.s32       d2,  d0,   d1
        vpaddl.s32      d3,  d2
        vmov.32         r0,  d3[0]
        bx              lr
endfunc


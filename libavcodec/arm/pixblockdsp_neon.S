/*
 * Copyright (c) 2020 Martin Storsjo
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

.macro vld1_8 dst, src, incr, aligned
.if \aligned
        vld1.8          {\dst}, [\src, :64], \incr
.else
        vld1.8          {\dst}, [\src], \incr
.endif
.endm

.macro get_pixels suffix, aligned
function ff_get_pixels\suffix\()_neon, export=1
        mov             r3,  #8
1:
        vld1_8          d0,  r1,  r2,  \aligned
        subs            r3,  r3,  #2
        vld1_8          d2,  r1,  r2,  \aligned
        vmovl.u8        q0,  d0
        vmovl.u8        q1,  d2
        vst1.16         {q0, q1}, [r0, :128]!
        bgt             1b

        bx              lr
endfunc
.endm

get_pixels , aligned=1
get_pixels _unaligned, aligned=0

.macro diff_pixels suffix, aligned=0
function ff_diff_pixels\suffix\()_neon, export=1
        mov             r12, #8
1:
        vld1_8          d0,  r1,  r3,  \aligned
        vld1_8          d1,  r2,  r3,  \aligned
        subs            r12, r12, #2
        vld1_8          d2,  r1,  r3,  \aligned
        vsubl.u8        q0,  d0,  d1
        vld1_8          d3,  r2,  r3,  \aligned
        vsubl.u8        q1,  d2,  d3
        vst1.16         {q0, q1}, [r0]!
        bgt             1b

        bx              lr
endfunc
.endm

diff_pixels , aligned=1
diff_pixels _unaligned, aligned=0

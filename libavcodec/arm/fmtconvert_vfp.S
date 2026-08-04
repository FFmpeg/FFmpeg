/*
 * Copyright (c) 2013 RISC OS Open Ltd <bavison@riscosopen.org>
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
#include "libavutil/arm/asm.S"

/**
 * ARM VFP optimised int32 to float conversion.
 * Assume len is a multiple of 8, destination buffer is at least 4 bytes aligned
 * (16 bytes alignment is best for BCM2835), little-endian.
 * TODO: could be further optimised by unrolling and interleaving, as above
 */
@ void ff_int32_to_float_fmul_scalar_vfp(float *dst, const int32_t *src, float mul, int len)
function ff_int32_to_float_fmul_scalar_vfp, export=1
VFP     tmp     .req    a4
VFP     len     .req    a3
NOVFP   tmp     .req    a3
NOVFP   len     .req    a4
NOVFP   vmov    s0, a3
        ldr             tmp, =0x03070000           @ RunFast mode, short vectors of length 8, stride 1
        fmrx            ip, FPSCR
        fmxr            FPSCR, tmp
1:
        vldmia          a2!, {s8-s15}
        vcvt.f32.s32    s8, s8
        vcvt.f32.s32    s9, s9
        vcvt.f32.s32    s10, s10
        vcvt.f32.s32    s11, s11
        vcvt.f32.s32    s12, s12
        vcvt.f32.s32    s13, s13
        vcvt.f32.s32    s14, s14
        vcvt.f32.s32    s15, s15
        vmul.f32        s8, s8, s0
        subs            len, len, #8
        vstmia          a1!, {s8-s11}
        vstmia          a1!, {s12-s15}
        bne             1b

        fmxr            FPSCR, ip
        bx              lr
endfunc
        .unreq  tmp
        .unreq  len

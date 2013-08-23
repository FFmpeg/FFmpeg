/*
 * Copyright (c) 2013 RISC OS Open Ltd <bavison@riscosopen.org>
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
#include "libavutil/arm/asm.S"

/**
 * ARM VFP optimised int32 to float conversion.
 * Assume len is a multiple of 8, destination buffer is at least 4 bytes aligned
 * (16 bytes alignment is best for BCM2835), little-endian.
 */
@ void ff_int32_to_float_fmul_array8_vfp(FmtConvertContext *c, float *dst, const int32_t *src, const float *mul, int len)
function ff_int32_to_float_fmul_array8_vfp, export=1
        push    {lr}
        ldr     a1, [sp, #4]
        subs    lr, a1, #3*8
        bcc     50f                        @ too short to pipeline
        @ Now need to find (len / 8) % 3. The approximation
        @ x / 24 = (x * 0xAB) >> 12
        @ is good for x < 4096, which is true for both AC3 and DCA.
        mov     a1, #0xAB
        ldr     ip, =0x03070000            @ RunFast mode, short vectors of length 8, stride 1
        mul     a1, lr, a1
        vpush   {s16-s31}
        mov     a1, a1, lsr #12
        add     a1, a1, a1, lsl #1
        rsb     a1, a1, lr, lsr #3
        cmp     a1, #1
        fmrx    a1, FPSCR
        fmxr    FPSCR, ip
        beq     11f
        blo     10f
        @ Array is (2 + multiple of 3) x 8 floats long
        @ drop through...
        vldmia          a3!, {s16-s23}
        vldmia          a4!, {s2,s3}
        vldmia          a3!, {s24-s31}
        vcvt.f32.s32    s16, s16
        vcvt.f32.s32    s17, s17
        vcvt.f32.s32    s18, s18
        vcvt.f32.s32    s19, s19
        vcvt.f32.s32    s20, s20
        vcvt.f32.s32    s21, s21
        vcvt.f32.s32    s22, s22
        vcvt.f32.s32    s23, s23
        vmul.f32        s16, s16, s2
        @ drop through...
3:
        vldmia          a3!, {s8-s15}
        vldmia          a4!, {s1}
        vcvt.f32.s32    s24, s24
        vcvt.f32.s32    s25, s25
        vcvt.f32.s32    s26, s26
        vcvt.f32.s32    s27, s27
        vcvt.f32.s32    s28, s28
        vcvt.f32.s32    s29, s29
        vcvt.f32.s32    s30, s30
        vcvt.f32.s32    s31, s31
        vmul.f32        s24, s24, s3
        vstmia          a2!, {s16-s19}
        vstmia          a2!, {s20-s23}
2:
        vldmia          a3!, {s16-s23}
        vldmia          a4!, {s2}
        vcvt.f32.s32    s8, s8
        vcvt.f32.s32    s9, s9
        vcvt.f32.s32    s10, s10
        vcvt.f32.s32    s11, s11
        vcvt.f32.s32    s12, s12
        vcvt.f32.s32    s13, s13
        vcvt.f32.s32    s14, s14
        vcvt.f32.s32    s15, s15
        vmul.f32        s8, s8, s1
        vstmia          a2!, {s24-s27}
        vstmia          a2!, {s28-s31}
1:
        vldmia          a3!, {s24-s31}
        vldmia          a4!, {s3}
        vcvt.f32.s32    s16, s16
        vcvt.f32.s32    s17, s17
        vcvt.f32.s32    s18, s18
        vcvt.f32.s32    s19, s19
        vcvt.f32.s32    s20, s20
        vcvt.f32.s32    s21, s21
        vcvt.f32.s32    s22, s22
        vcvt.f32.s32    s23, s23
        vmul.f32        s16, s16, s2
        vstmia          a2!, {s8-s11}
        vstmia          a2!, {s12-s15}

        subs            lr, lr, #8*3
        bpl             3b

        vcvt.f32.s32    s24, s24
        vcvt.f32.s32    s25, s25
        vcvt.f32.s32    s26, s26
        vcvt.f32.s32    s27, s27
        vcvt.f32.s32    s28, s28
        vcvt.f32.s32    s29, s29
        vcvt.f32.s32    s30, s30
        vcvt.f32.s32    s31, s31
        vmul.f32        s24, s24, s3
        vstmia          a2!, {s16-s19}
        vstmia          a2!, {s20-s23}
        vstmia          a2!, {s24-s27}
        vstmia          a2!, {s28-s31}

        fmxr    FPSCR, a1
        vpop    {s16-s31}
        pop     {pc}

10:     @ Array is (multiple of 3) x 8 floats long
        vldmia          a3!, {s8-s15}
        vldmia          a4!, {s1,s2}
        vldmia          a3!, {s16-s23}
        vcvt.f32.s32    s8, s8
        vcvt.f32.s32    s9, s9
        vcvt.f32.s32    s10, s10
        vcvt.f32.s32    s11, s11
        vcvt.f32.s32    s12, s12
        vcvt.f32.s32    s13, s13
        vcvt.f32.s32    s14, s14
        vcvt.f32.s32    s15, s15
        vmul.f32        s8, s8, s1
        b               1b

11:     @ Array is (1 + multiple of 3) x 8 floats long
        vldmia          a3!, {s24-s31}
        vldmia          a4!, {s3}
        vldmia          a3!, {s8-s15}
        vldmia          a4!, {s1}
        vcvt.f32.s32    s24, s24
        vcvt.f32.s32    s25, s25
        vcvt.f32.s32    s26, s26
        vcvt.f32.s32    s27, s27
        vcvt.f32.s32    s28, s28
        vcvt.f32.s32    s29, s29
        vcvt.f32.s32    s30, s30
        vcvt.f32.s32    s31, s31
        vmul.f32        s24, s24, s3
        b               2b

50:
        ldr     lr, =0x03070000         @ RunFast mode, short vectors of length 8, stride 1
        fmrx    ip, FPSCR
        fmxr    FPSCR, lr
51:
        vldmia          a3!, {s8-s15}
        vldmia          a4!, {s0}
        vcvt.f32.s32    s8, s8
        vcvt.f32.s32    s9, s9
        vcvt.f32.s32    s10, s10
        vcvt.f32.s32    s11, s11
        vcvt.f32.s32    s12, s12
        vcvt.f32.s32    s13, s13
        vcvt.f32.s32    s14, s14
        vcvt.f32.s32    s15, s15
        vmul.f32        s8, s8, s0
        subs            a1, a1, #8
        vstmia          a2!, {s8-s11}
        vstmia          a2!, {s12-s15}
        bne             51b

        fmxr    FPSCR, ip
        pop     {pc}
endfunc

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
        ldr     tmp, =0x03070000           @ RunFast mode, short vectors of length 8, stride 1
        fmrx    ip, FPSCR
        fmxr    FPSCR, tmp
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

        fmxr    FPSCR, ip
        bx      lr
endfunc
        .unreq  tmp
        .unreq  len

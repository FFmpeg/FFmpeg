/*
 * Copyright (c) 2013 RISC OS Open Ltd
 * Author: Ben Avison <bavison@riscosopen.org>
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

#include "libavutil/arm/asm.S"

@ TODO: * FFTs wider than 16
@       * dispatch code

function fft4_vfp
        vldr    d0, [a1, #0*2*4]   @ s0,s1   = z[0]
        vldr    d4, [a1, #1*2*4]   @ s8,s9   = z[1]
        vldr    d1, [a1, #2*2*4]   @ s2,s3   = z[2]
        vldr    d5, [a1, #3*2*4]   @ s10,s11 = z[3]
        @ stall
        vadd.f  s12, s0, s8        @ i0
        vadd.f  s13, s1, s9        @ i1
        vadd.f  s14, s2, s10       @ i2
        vadd.f  s15, s3, s11       @ i3
        vsub.f  s8, s0, s8         @ i4
        vsub.f  s9, s1, s9         @ i5
        vsub.f  s10, s2, s10       @ i6
        vsub.f  s11, s3, s11       @ i7
        @ stall
        @ stall
        vadd.f  s0, s12, s14       @ z[0].re
        vsub.f  s4, s12, s14       @ z[2].re
        vadd.f  s1, s13, s15       @ z[0].im
        vsub.f  s5, s13, s15       @ z[2].im
        vadd.f  s7, s9, s10        @ z[3].im
        vsub.f  s3, s9, s10        @ z[1].im
        vadd.f  s2, s8, s11        @ z[1].re
        vsub.f  s6, s8, s11        @ z[3].re
        @ stall
        @ stall
        vstr    d0, [a1, #0*2*4]
        vstr    d2, [a1, #2*2*4]
        @ stall
        @ stall
        vstr    d1, [a1, #1*2*4]
        vstr    d3, [a1, #3*2*4]

        bx      lr
endfunc

.macro macro_fft8_head
        @ FFT4
        vldr    d4, [a1, #0 * 2*4]
        vldr    d6, [a1, #1 * 2*4]
        vldr    d5, [a1, #2 * 2*4]
        vldr    d7, [a1, #3 * 2*4]
            @ BF
            vldr    d12, [a1, #4 * 2*4]
        vadd.f  s16, s8, s12    @ vector op
            vldr    d14, [a1, #5 * 2*4]
            vldr    d13, [a1, #6 * 2*4]
            vldr    d15, [a1, #7 * 2*4]
        vsub.f  s20, s8, s12    @ vector op
        vadd.f  s0, s16, s18
        vsub.f  s2, s16, s18
        vadd.f  s1, s17, s19
        vsub.f  s3, s17, s19
        vadd.f  s7, s21, s22
        vsub.f  s5, s21, s22
        vadd.f  s4, s20, s23
        vsub.f  s6, s20, s23
            vsub.f  s20, s24, s28   @ vector op
        vstr    d0, [a1, #0 * 2*4]  @ transfer s0-s7 to s24-s31 via memory
        vstr    d1, [a1, #1 * 2*4]
        vldr    s0, cos1pi4
            vadd.f  s16, s24, s28   @ vector op
        vstr    d2, [a1, #2 * 2*4]
        vstr    d3, [a1, #3 * 2*4]
        vldr    d12, [a1, #0 * 2*4]
            @ TRANSFORM
            vmul.f  s20, s20, s0    @ vector x scalar op
        vldr    d13, [a1, #1 * 2*4]
        vldr    d14, [a1, #2 * 2*4]
        vldr    d15, [a1, #3 * 2*4]
        @ BUTTERFLIES
        vadd.f  s0, s18, s16
        vadd.f  s1, s17, s19
        vsub.f  s2, s17, s19
        vsub.f  s3, s18, s16
            vadd.f  s4, s21, s20
            vsub.f  s5, s21, s20
            vadd.f  s6, s22, s23
            vsub.f  s7, s22, s23
        vadd.f  s8, s0, s24         @ vector op
        vstr    d0, [a1, #0 * 2*4]  @ transfer s0-s3 to s12-s15 via memory
        vstr    d1, [a1, #1 * 2*4]
        vldr    d6, [a1, #0 * 2*4]
        vldr    d7, [a1, #1 * 2*4]
            vadd.f  s1, s5, s6
            vadd.f  s0, s7, s4
            vsub.f  s2, s5, s6
            vsub.f  s3, s7, s4
        vsub.f  s12, s24, s12       @ vector op
            vsub.f  s5, s29, s1
            vsub.f  s4, s28, s0
            vsub.f  s6, s30, s2
            vsub.f  s7, s31, s3
            vadd.f  s16, s0, s28    @ vector op
        vstr    d6, [a1, #4 * 2*4]
        vstr    d7, [a1, #6 * 2*4]
        vstr    d4, [a1, #0 * 2*4]
        vstr    d5, [a1, #2 * 2*4]
             vstr    d2, [a1, #5 * 2*4]
             vstr    d3, [a1, #7 * 2*4]
.endm

.macro macro_fft8_tail
             vstr    d8, [a1, #1 * 2*4]
             vstr    d9, [a1, #3 * 2*4]
.endm

function fft8_vfp
        ldr     a3, =0x03030000     @ RunFast mode, vector length 4, stride 1
        fmrx    a2, FPSCR
        fmxr    FPSCR, a3
        vpush   {s16-s31}

        macro_fft8_head
        macro_fft8_tail

        vpop    {s16-s31}
        fmxr    FPSCR, a2
        bx      lr
endfunc

.align 3
cos1pi4:    @ cos(1*pi/4) = sqrt(2)
        .float  0.707106769084930419921875
cos1pi8:    @ cos(1*pi/8) = sqrt(2+sqrt(2))/2
        .float  0.92387950420379638671875
cos3pi8:    @ cos(2*pi/8) = sqrt(2-sqrt(2))/2
        .float  0.3826834261417388916015625

function ff_fft16_vfp, export=1
        ldr     a3, =0x03030000     @ RunFast mode, vector length 4, stride 1
        fmrx    a2, FPSCR
        fmxr    FPSCR, a3
        vpush   {s16-s31}

        macro_fft8_head
        @ FFT4(z+8)
        vldr    d10, [a1, #8 * 2*4]
        vldr    d12, [a1, #9 * 2*4]
        vldr    d11, [a1, #10 * 2*4]
        vldr    d13, [a1, #11 * 2*4]
        macro_fft8_tail
        vadd.f  s16, s20, s24   @ vector op
            @ FFT4(z+12)
            vldr    d4, [a1, #12 * 2*4]
            vldr    d6, [a1, #13 * 2*4]
            vldr    d5, [a1, #14 * 2*4]
        vsub.f  s20, s20, s24   @ vector op
            vldr    d7, [a1, #15 * 2*4]
        vadd.f  s0, s16, s18
        vsub.f  s4, s16, s18
        vadd.f  s1, s17, s19
        vsub.f  s5, s17, s19
        vadd.f  s7, s21, s22
        vsub.f  s3, s21, s22
        vadd.f  s2, s20, s23
        vsub.f  s6, s20, s23
            vadd.f  s16, s8, s12    @ vector op
        vstr    d0, [a1, #8 * 2*4]
        vstr    d2, [a1, #10 * 2*4]
        vstr    d1, [a1, #9 * 2*4]
            vsub.f  s20, s8, s12
        vstr    d3, [a1, #11 * 2*4]
        @ TRANSFORM(z[2],z[6],z[10],z[14],cos1pi4,cos1pi4)
        vldr    d12, [a1, #10 * 2*4]
            vadd.f  s0, s16, s18
            vadd.f  s1, s17, s19
            vsub.f  s6, s16, s18
            vsub.f  s7, s17, s19
            vsub.f  s3, s21, s22
            vadd.f  s2, s20, s23
            vadd.f  s5, s21, s22
            vsub.f  s4, s20, s23
            vstr    d0, [a1, #12 * 2*4]
        vmov    s0, s6
          @ TRANSFORM(z[1],z[5],z[9],z[13],cos1pi8,cos3pi8)
          vldr    d6, [a1, #9 * 2*4]
            vstr    d1, [a1, #13 * 2*4]
        vldr    d1, cos1pi4 @ s2 = cos1pi4, s3 = cos1pi8
            vstr    d2, [a1, #15 * 2*4]
          vldr    d7, [a1, #13 * 2*4]
        vadd.f  s4, s25, s24
        vsub.f  s5, s25, s24
        vsub.f  s6, s0, s7
        vadd.f  s7, s0, s7
          vmul.f  s20, s12, s3  @ vector op
            @ TRANSFORM(z[3],z[7],z[11],z[15],cos3pi8,cos1pi8)
            vldr    d4, [a1, #11 * 2*4]
            vldr    d5, [a1, #15 * 2*4]
            vldr    s1, cos3pi8
        vmul.f  s24, s4, s2     @ vector * scalar op
          vmul.f  s28, s12, s1  @ vector * scalar op
            vmul.f  s12, s8, s1 @ vector * scalar op
          vadd.f  s4, s20, s29
          vsub.f  s5, s21, s28
          vsub.f  s6, s22, s31
          vadd.f  s7, s23, s30
            vmul.f  s8, s8, s3  @ vector * scalar op
          vldr    d8, [a1, #1 * 2*4]
          vldr    d9, [a1, #5 * 2*4]
            vldr    d10, [a1, #3 * 2*4]
            vldr    d11, [a1, #7 * 2*4]
        vldr    d14, [a1, #2 * 2*4]
          vadd.f  s0, s6, s4
          vadd.f  s1, s5, s7
          vsub.f  s2, s5, s7
          vsub.f  s3, s6, s4
            vadd.f  s4, s12, s9
            vsub.f  s5, s13, s8
            vsub.f  s6, s14, s11
            vadd.f  s7, s15, s10
          vadd.f  s12, s0, s16  @ vector op
          vstr    d0, [a1, #1 * 2*4]
          vstr    d1, [a1, #5 * 2*4]
          vldr    d4, [a1, #1 * 2*4]
          vldr    d5, [a1, #5 * 2*4]
            vadd.f  s0, s6, s4
            vadd.f  s1, s5, s7
            vsub.f  s2, s5, s7
            vsub.f  s3, s6, s4
          vsub.f  s8, s16, s8   @ vector op
          vstr    d6, [a1, #1 * 2*4]
          vstr    d7, [a1, #5 * 2*4]
        vldr    d15, [a1, #6 * 2*4]
            vsub.f  s4, s20, s0
            vsub.f  s5, s21, s1
            vsub.f  s6, s22, s2
            vsub.f  s7, s23, s3
            vadd.f  s20, s0, s20    @ vector op
          vstr    d4, [a1, #9 * 2*4]
              @ TRANSFORM_ZERO(z[0],z[4],z[8],z[12])
              vldr    d6, [a1, #8 * 2*4]
          vstr    d5, [a1, #13 * 2*4]
              vldr    d7, [a1, #12 * 2*4]
          vstr    d2, [a1, #11 * 2*4]
              vldr    d8, [a1, #0 * 2*4]
          vstr    d3, [a1, #15 * 2*4]
              vldr    d9, [a1, #4 * 2*4]
        vadd.f  s0, s26, s24
        vadd.f  s1, s25, s27
        vsub.f  s2, s25, s27
        vsub.f  s3, s26, s24
              vadd.f  s4, s14, s12
              vadd.f  s5, s13, s15
              vsub.f  s6, s13, s15
              vsub.f  s7, s14, s12
        vadd.f  s8, s0, s28 @ vector op
        vstr    d0, [a1, #3 * 2*4]
        vstr    d1, [a1, #7 * 2*4]
        vldr    d6, [a1, #3 * 2*4]
        vldr    d7, [a1, #7 * 2*4]
              vsub.f  s0, s16, s4
              vsub.f  s1, s17, s5
              vsub.f  s2, s18, s6
              vsub.f  s3, s19, s7
        vsub.f  s12, s28, s12       @ vector op
              vadd.f  s16, s4, s16  @ vector op
            vstr    d10, [a1, #3 * 2*4]
            vstr    d11, [a1, #7 * 2*4]
        vstr    d4, [a1, #2 * 2*4]
        vstr    d5, [a1, #6 * 2*4]
              vstr    d0, [a1, #8 * 2*4]
              vstr    d1, [a1, #12 * 2*4]
        vstr    d6, [a1, #10 * 2*4]
        vstr    d7, [a1, #14 * 2*4]
              vstr    d8, [a1, #0 * 2*4]
              vstr    d9, [a1, #4 * 2*4]

        vpop    {s16-s31}
        fmxr    FPSCR, a2
        bx      lr
endfunc

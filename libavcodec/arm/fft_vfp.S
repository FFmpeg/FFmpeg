/*
 * Copyright (c) 2013 RISC OS Open Ltd
 * Author: Ben Avison <bavison@riscosopen.org>
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

@ The fftx_internal_vfp versions of the functions obey a modified AAPCS:
@ VFP is in RunFast mode, vector length 4, stride 1 thoroughout, and
@ all single-precision VFP registers may be corrupted on exit. The a2
@ register may not be clobbered in these functions, as it holds the
@ stored original FPSCR.

function ff_fft_calc_vfp, export=1
        ldr     ip, [a1, #0]    @ nbits
        mov     a1, a2
A       ldr     pc, [pc, ip, lsl #2]
A       .word   0
A       .word   0
A       .word   0
T       movrel  a2, (fft_tab_vfp - 8)
T       ldr     pc, [a2, ip, lsl #2]
T endfunc
T const fft_tab_vfp
        .word   fft4_vfp
        .word   fft8_vfp
        .word   X(ff_fft16_vfp)     @ this one alone is exported
        .word   fft32_vfp
        .word   fft64_vfp
        .word   fft128_vfp
        .word   fft256_vfp
        .word   fft512_vfp
        .word   fft1024_vfp
        .word   fft2048_vfp
        .word   fft4096_vfp
        .word   fft8192_vfp
        .word   fft16384_vfp
        .word   fft32768_vfp
        .word   fft65536_vfp
A endfunc

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

function .Lfft8_internal_vfp
        macro_fft8_head
        macro_fft8_tail
        bx      lr
endfunc

function fft8_vfp
        ldr     a3, =0x03030000     @ RunFast mode, vector length 4, stride 1
        fmrx    a2, FPSCR
        fmxr    FPSCR, a3
        vpush   {s16-s31}
        mov     ip, lr
        bl      .Lfft8_internal_vfp
        vpop    {s16-s31}
        fmxr    FPSCR, a2
        bx      ip
endfunc

.align 3
cos1pi4:    @ cos(1*pi/4) = sqrt(2)
        .float  0.707106769084930419921875
cos1pi8:    @ cos(1*pi/8) = sqrt(2+sqrt(2))/2
        .float  0.92387950420379638671875
cos3pi8:    @ cos(2*pi/8) = sqrt(2-sqrt(2))/2
        .float  0.3826834261417388916015625

function .Lfft16_internal_vfp
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

        bx      lr
endfunc

function ff_fft16_vfp, export=1
        ldr     a3, =0x03030000     @ RunFast mode, vector length 4, stride 1
        fmrx    a2, FPSCR
        fmxr    FPSCR, a3
        vpush   {s16-s31}
        mov     ip, lr
        bl      .Lfft16_internal_vfp
        vpop    {s16-s31}
        fmxr    FPSCR, a2
        bx      ip
endfunc

.macro pass n, z0, z1, z2, z3
        add     v6, v5, #4*2*\n
        @ TRANSFORM_ZERO(z[0],z[o1],z[o2],z[o3])
            @ TRANSFORM(z[1],z[o1+1],z[o2+1],z[o3+1],wre[1],wim[-1])
                @ TRANSFORM(z[0],z[o1],z[o2],z[o3],wre[0],wim[0])
                    @ TRANSFORM(z[1],z[o1+1],z[o2+1],z[o3+1],wre[1],wim[-1])
            vldr    d8, [\z2, #8*(o2+1)]        @ s16,s17
            vldmdb  v6!, {s2}
            vldr    d9, [\z3, #8*(o3+1)]        @ s18,s19
            vldmia  v5!, {s0,s1}                @ s0 is unused
        vldr    s7, [\z2, #8*o2]            @ t1
            vmul.f  s20, s16, s2                @ vector * scalar
        vldr    s0, [\z3, #8*o3]            @ t5
        vldr    s6, [\z2, #8*o2+4]          @ t2
        vldr    s3, [\z3, #8*o3+4]          @ t6
            vmul.f  s16, s16, s1                @ vector * scalar
        ldr     a4, =\n-1
1:      add     \z0, \z0, #8*2
 .if \n*4*2 >= 512
        add     \z1, \z1, #8*2
 .endif
 .if \n*4*2 >= 256
        add     \z2, \z2, #8*2
 .endif
 .if \n*4*2 >= 512
        add     \z3, \z3, #8*2
 .endif
        @ up to 2 stalls (VFP vector issuing / waiting for s0)
        @ depending upon whether this is the first iteration and
        @ how many add instructions are inserted above
        vadd.f  s4, s0, s7                  @ t5
        vadd.f  s5, s6, s3                  @ t6
        vsub.f  s6, s6, s3                  @ t4
        vsub.f  s7, s0, s7                  @ t3
        vldr    d6, [\z0, #8*0-8*2]         @ s12,s13
            vadd.f  s0, s16, s21                @ t1
        vldr    d7, [\z1, #8*o1-8*2]        @ s14,s15
            vsub.f  s1, s18, s23                @ t5
        vadd.f  s8, s4, s12                 @ vector + vector
        @ stall (VFP vector issuing)
        @ stall (VFP vector issuing)
        @ stall (VFP vector issuing)
        vsub.f  s4, s12, s4
        vsub.f  s5, s13, s5
        vsub.f  s6, s14, s6
        vsub.f  s7, s15, s7
            vsub.f  s2, s17, s20                @ t2
            vadd.f  s3, s19, s22                @ t6
        vstr    d4, [\z0, #8*0-8*2]         @ s8,s9
        vstr    d5, [\z1, #8*o1-8*2]        @ s10,s11
        @ stall (waiting for s5)
        vstr    d2, [\z2, #8*o2-8*2]        @ s4,s5
            vadd.f  s4, s1, s0                  @ t5
        vstr    d3, [\z3, #8*o3-8*2]        @ s6,s7
            vsub.f  s7, s1, s0                  @ t3
            vadd.f  s5, s2, s3                  @ t6
            vsub.f  s6, s2, s3                  @ t4
            vldr    d6, [\z0, #8*1-8*2]         @ s12,s13
            vldr    d7, [\z1, #8*(o1+1)-8*2]    @ s14,s15
                vldr    d4, [\z2, #8*o2]            @ s8,s9
                vldmdb  v6!, {s2,s3}
                vldr    d5, [\z3, #8*o3]            @ s10,s11
            vadd.f  s20, s4, s12                @ vector + vector
                vldmia  v5!, {s0,s1}
                    vldr    d8, [\z2, #8*(o2+1)]        @ s16,s17
            @ stall (VFP vector issuing)
            vsub.f  s4, s12, s4
            vsub.f  s5, s13, s5
            vsub.f  s6, s14, s6
            vsub.f  s7, s15, s7
                vmul.f  s12, s8, s3                 @ vector * scalar
            vstr    d10, [\z0, #8*1-8*2]        @ s20,s21
                    vldr    d9, [\z3, #8*(o3+1)]        @ s18,s19
            vstr    d11, [\z1, #8*(o1+1)-8*2]   @ s22,s23
                vmul.f  s8, s8, s0                  @ vector * scalar
            vstr    d2, [\z2, #8*(o2+1)-8*2]    @ s4,s5
            @ stall (waiting for s7)
            vstr    d3, [\z3, #8*(o3+1)-8*2]    @ s6,s7
                    vmul.f  s20, s16, s2                @ vector * scalar
                @ stall (VFP vector issuing)
                @ stall (VFP vector issuing)
                @ stall (VFP vector issuing)
                vadd.f  s7, s8, s13                 @ t1
                vsub.f  s6, s9, s12                 @ t2
                vsub.f  s0, s10, s15                @ t5
                vadd.f  s3, s11, s14                @ t6
                    vmul.f  s16, s16, s1                @ vector * scalar
        subs    a4, a4, #1
        bne     1b
        @ What remains is identical to the first two indentations of
        @ the above, but without the increment of z
        vadd.f  s4, s0, s7                  @ t5
        vadd.f  s5, s6, s3                  @ t6
        vsub.f  s6, s6, s3                  @ t4
        vsub.f  s7, s0, s7                  @ t3
        vldr    d6, [\z0, #8*0]             @ s12,s13
            vadd.f  s0, s16, s21                @ t1
        vldr    d7, [\z1, #8*o1]            @ s14,s15
            vsub.f  s1, s18, s23                @ t5
        vadd.f  s8, s4, s12                 @ vector + vector
        vsub.f  s4, s12, s4
        vsub.f  s5, s13, s5
        vsub.f  s6, s14, s6
        vsub.f  s7, s15, s7
            vsub.f  s2, s17, s20                @ t2
            vadd.f  s3, s19, s22                @ t6
        vstr    d4, [\z0, #8*0]             @ s8,s9
        vstr    d5, [\z1, #8*o1]            @ s10,s11
        vstr    d2, [\z2, #8*o2]            @ s4,s5
            vadd.f  s4, s1, s0                  @ t5
        vstr    d3, [\z3, #8*o3]            @ s6,s7
            vsub.f  s7, s1, s0                  @ t3
            vadd.f  s5, s2, s3                  @ t6
            vsub.f  s6, s2, s3                  @ t4
            vldr    d6, [\z0, #8*1]             @ s12,s13
            vldr    d7, [\z1, #8*(o1+1)]        @ s14,s15
            vadd.f  s20, s4, s12                @ vector + vector
            vsub.f  s4, s12, s4
            vsub.f  s5, s13, s5
            vsub.f  s6, s14, s6
            vsub.f  s7, s15, s7
            vstr    d10, [\z0, #8*1]            @ s20,s21
            vstr    d11, [\z1, #8*(o1+1)]       @ s22,s23
            vstr    d2, [\z2, #8*(o2+1)]        @ s4,s5
            vstr    d3, [\z3, #8*(o3+1)]        @ s6,s7
.endm

.macro  def_fft n, n2, n4
function .Lfft\n\()_internal_vfp
 .if \n >= 512
        push    {v1-v6,lr}
 .elseif \n >= 256
        push    {v1-v2,v5-v6,lr}
 .else
        push    {v1,v5-v6,lr}
 .endif
        mov     v1, a1
        bl      .Lfft\n2\()_internal_vfp
        add     a1, v1, #8*(\n/4)*2
        bl      .Lfft\n4\()_internal_vfp
        movrelx v5, X(ff_cos_\n), a1
        add     a1, v1, #8*(\n/4)*3
        bl      .Lfft\n4\()_internal_vfp
 .if \n >= 512
  .set o1, 0*(\n/4/2)
  .set o2, 0*(\n/4/2)
  .set o3, 0*(\n/4/2)
        add     v2, v1, #8*2*(\n/4/2)
        add     v3, v1, #8*4*(\n/4/2)
        add     v4, v1, #8*6*(\n/4/2)
        pass    (\n/4/2), v1, v2, v3, v4
        pop     {v1-v6,pc}
 .elseif \n >= 256
  .set o1, 2*(\n/4/2)
  .set o2, 0*(\n/4/2)
  .set o3, 2*(\n/4/2)
        add     v2, v1, #8*4*(\n/4/2)
        pass    (\n/4/2), v1, v1, v2, v2
        pop     {v1-v2,v5-v6,pc}
 .else
  .set o1, 2*(\n/4/2)
  .set o2, 4*(\n/4/2)
  .set o3, 6*(\n/4/2)
        pass    (\n/4/2), v1, v1, v1, v1
        pop     {v1,v5-v6,pc}
 .endif
endfunc

function fft\n\()_vfp
        ldr     a3, =0x03030000 /* RunFast mode, vector length 4, stride 1 */
        fmrx    a2, FPSCR
        fmxr    FPSCR, a3
        vpush   {s16-s31}
        mov     ip, lr
        bl      .Lfft\n\()_internal_vfp
        vpop    {s16-s31}
        fmxr    FPSCR, a2
        bx      ip
endfunc

.ltorg
.endm

        def_fft    32,    16,     8
        def_fft    64,    32,    16
        def_fft   128,    64,    32
        def_fft   256,   128,    64
        def_fft   512,   256,   128
        def_fft  1024,   512,   256
        def_fft  2048,  1024,   512
        def_fft  4096,  2048,  1024
        def_fft  8192,  4096,  2048
        def_fft 16384,  8192,  4096
        def_fft 32768, 16384,  8192
        def_fft 65536, 32768, 16384

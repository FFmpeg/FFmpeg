/*
 * ARM NEON optimised FFT
 *
 * Copyright (c) 2009 Mans Rullgard <mans@mansr.com>
 * Copyright (c) 2009 Naotoshi Nojiri
 *
 * This algorithm (though not any of the implementation details) is
 * based on libdjbfft by D. J. Bernstein.
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

#include "asm.S"

#define M_SQRT1_2 0.70710678118654752440


function fft4_neon
        vld1.32         {d0-d3}, [r0,:128]

        vext.32         q8,  q1,  q1,  #1       @ i2,r3 d3=i3,r2
        vsub.f32        d6,  d0,  d1            @ r0-r1,i0-i1
        vsub.f32        d7,  d16, d17           @ r3-r2,i2-i3
        vadd.f32        d4,  d0,  d1            @ r0+r1,i0+i1
        vadd.f32        d5,  d2,  d3            @ i2+i3,r2+r3
        vadd.f32        d1,  d6,  d7
        vsub.f32        d3,  d6,  d7
        vadd.f32        d0,  d4,  d5
        vsub.f32        d2,  d4,  d5

        vst1.32         {d0-d3}, [r0,:128]

        bx              lr
endfunc

function fft8_neon
        mov             r1,  r0
        vld1.32         {d0-d3},   [r1,:128]!
        vld1.32         {d16-d19}, [r1,:128]

        movw            r2,  #0x04f3            @ sqrt(1/2)
        movt            r2,  #0x3f35
        eor             r3,  r2,  #1<<31
        vdup.32         d31, r2

        vext.32         q11, q1,  q1,  #1       @ i2,r3,i3,r2
        vadd.f32        d4,  d16, d17           @ r4+r5,i4+i5
        vmov            d28, r3,  r2
        vadd.f32        d5,  d18, d19           @ r6+r7,i6+i7
        vsub.f32        d17, d16, d17           @ r4-r5,i4-i5
        vsub.f32        d19, d18, d19           @ r6-r7,i6-i7
        vrev64.32       d29, d28
        vadd.f32        d20, d0,  d1            @ r0+r1,i0+i1
        vadd.f32        d21, d2,  d3            @ r2+r3,i2+i3
        vmul.f32        d26, d17, d28           @ -a2r*w,a2i*w
        vext.32         q3,  q2,  q2,  #1
        vmul.f32        d27, d19, d29           @ a3r*w,-a3i*w
        vsub.f32        d23, d22, d23           @ i2-i3,r3-r2
        vsub.f32        d22, d0,  d1            @ r0-r1,i0-i1
        vmul.f32        d24, d17, d31           @ a2r*w,a2i*w
        vmul.f32        d25, d19, d31           @ a3r*w,a3i*w
        vadd.f32        d0,  d20, d21
        vsub.f32        d2,  d20, d21
        vadd.f32        d1,  d22, d23
        vrev64.32       q13, q13
        vsub.f32        d3,  d22, d23
        vsub.f32        d6,  d6,  d7
        vadd.f32        d24, d24, d26           @ a2r+a2i,a2i-a2r   t1,t2
        vadd.f32        d25, d25, d27           @ a3r-a3i,a3i+a3r   t5,t6
        vadd.f32        d7,  d4,  d5
        vsub.f32        d18, d2,  d6
        vext.32         q13, q12, q12, #1
        vadd.f32        d2,  d2,  d6
        vsub.f32        d16, d0,  d7
        vadd.f32        d5,  d25, d24
        vsub.f32        d4,  d26, d27
        vadd.f32        d0,  d0,  d7
        vsub.f32        d17, d1,  d5
        vsub.f32        d19, d3,  d4
        vadd.f32        d3,  d3,  d4
        vadd.f32        d1,  d1,  d5

        vst1.32         {d16-d19}, [r1,:128]
        vst1.32         {d0-d3},   [r0,:128]

        bx              lr
endfunc

function fft16_neon
        movrel          r1, mppm
        vld1.32         {d16-d19}, [r0,:128]!   @ q8{r0,i0,r1,i1} q9{r2,i2,r3,i3}
        pld             [r0, #32]
        vld1.32         {d2-d3}, [r1,:128]
        vext.32         q13, q9,  q9,  #1
        vld1.32         {d22-d25}, [r0,:128]!   @ q11{r4,i4,r5,i5} q12{r6,i5,r7,i7}
        vadd.f32        d4,  d16, d17
        vsub.f32        d5,  d16, d17
        vadd.f32        d18, d18, d19
        vsub.f32        d19, d26, d27

        vadd.f32        d20, d22, d23
        vsub.f32        d22, d22, d23
        vsub.f32        d23, d24, d25
        vadd.f32        q8,  q2,  q9            @ {r0,i0,r1,i1}
        vadd.f32        d21, d24, d25
        vmul.f32        d24, d22, d2
        vsub.f32        q9,  q2,  q9            @ {r2,i2,r3,i3}
        vmul.f32        d25, d23, d3
        vuzp.32         d16, d17                @ {r0,r1,i0,i1}
        vmul.f32        q1,  q11, d2[1]
        vuzp.32         d18, d19                @ {r2,r3,i2,i3}
        vrev64.32       q12, q12
        vadd.f32        q11, q12, q1            @ {t1a,t2a,t5,t6}
        vld1.32         {d24-d27}, [r0,:128]!   @ q12{r8,i8,r9,i9} q13{r10,i10,r11,i11}
        vzip.32         q10, q11
        vld1.32         {d28-d31}, [r0,:128]    @ q14{r12,i12,r13,i13} q15{r14,i14,r15,i15}
        vadd.f32        d0,  d22, d20
        vadd.f32        d1,  d21, d23
        vsub.f32        d2,  d21, d23
        vsub.f32        d3,  d22, d20
        sub             r0,  r0,  #96
        vext.32         q13, q13, q13, #1
        vsub.f32        q10, q8,  q0            @ {r4,r5,i4,i5}
        vadd.f32        q8,  q8,  q0            @ {r0,r1,i0,i1}
        vext.32         q15, q15, q15, #1
        vsub.f32        q11, q9,  q1            @ {r6,r7,i6,i7}
        vswp            d25, d26                @ q12{r8,i8,i10,r11} q13{r9,i9,i11,r10}
        vadd.f32        q9,  q9,  q1            @ {r2,r3,i2,i3}
        vswp            d29, d30                @ q14{r12,i12,i14,r15} q15{r13,i13,i15,r14}
        vadd.f32        q0,  q12, q13           @ {t1,t2,t5,t6}
        vadd.f32        q1,  q14, q15           @ {t1a,t2a,t5a,t6a}
        movrel          r2,  X(ff_cos_16)
        vsub.f32        q13, q12, q13           @ {t3,t4,t7,t8}
        vrev64.32       d1,  d1
        vsub.f32        q15, q14, q15           @ {t3a,t4a,t7a,t8a}
        vrev64.32       d3,  d3
        movrel          r3,  pmmp
        vswp            d1,  d26                @ q0{t1,t2,t3,t4} q13{t6,t5,t7,t8}
        vswp            d3,  d30                @ q1{t1a,t2a,t3a,t4a} q15{t6a,t5a,t7a,t8a}
        vadd.f32        q12, q0,  q13           @ {r8,i8,r9,i9}
        vadd.f32        q14, q1,  q15           @ {r12,i12,r13,i13}
        vld1.32         {d4-d5},  [r2,:64]
        vsub.f32        q13, q0,  q13           @ {r10,i10,r11,i11}
        vsub.f32        q15, q1,  q15           @ {r14,i14,r15,i15}
        vswp            d25, d28                @ q12{r8,i8,r12,i12} q14{r9,i9,r13,i13}
        vld1.32         {d6-d7},  [r3,:128]
        vrev64.32       q1,  q14
        vmul.f32        q14, q14, d4[1]
        vmul.f32        q1,  q1,  q3
        vmla.f32        q14, q1,  d5[1]         @ {t1a,t2a,t5a,t6a}
        vswp            d27, d30                @ q13{r10,i10,r14,i14} q15{r11,i11,r15,i15}
        vzip.32         q12, q14
        vadd.f32        d0,  d28, d24
        vadd.f32        d1,  d25, d29
        vsub.f32        d2,  d25, d29
        vsub.f32        d3,  d28, d24
        vsub.f32        q12, q8,  q0            @ {r8,r9,i8,i9}
        vadd.f32        q8,  q8,  q0            @ {r0,r1,i0,i1}
        vsub.f32        q14, q10, q1            @ {r12,r13,i12,i13}
        mov             r1,  #32
        vadd.f32        q10, q10, q1            @ {r4,r5,i4,i5}
        vrev64.32       q0,  q13
        vmul.f32        q13, q13, d5[0]
        vrev64.32       q1,  q15
        vmul.f32        q15, q15, d5[1]
        vst2.32         {d16-d17},[r0,:128], r1
        vmul.f32        q0,  q0,  q3
        vst2.32         {d20-d21},[r0,:128], r1
        vmul.f32        q1,  q1,  q3
        vmla.f32        q13, q0,  d5[0]         @ {t1,t2,t5,t6}
        vmla.f32        q15, q1,  d4[1]         @ {t1a,t2a,t5a,t6a}
        vst2.32         {d24-d25},[r0,:128], r1
        vst2.32         {d28-d29},[r0,:128]
        vzip.32         q13, q15
        sub             r0, r0, #80
        vadd.f32        d0,  d30, d26
        vadd.f32        d1,  d27, d31
        vsub.f32        d2,  d27, d31
        vsub.f32        d3,  d30, d26
        vsub.f32        q13, q9,  q0            @ {r10,r11,i10,i11}
        vadd.f32        q9,  q9,  q0            @ {r2,r3,i2,i3}
        vsub.f32        q15, q11, q1            @ {r14,r15,i14,i15}
        vadd.f32        q11, q11, q1            @ {r6,r7,i6,i7}
        vst2.32         {d18-d19},[r0,:128], r1
        vst2.32         {d22-d23},[r0,:128], r1
        vst2.32         {d26-d27},[r0,:128], r1
        vst2.32         {d30-d31},[r0,:128]
        bx              lr
endfunc

function fft_pass_neon
        push            {r4-r6,lr}
        mov             r6,  r2                 @ n
        lsl             r5,  r2,  #3            @ 2 * n * sizeof FFTSample
        lsl             r4,  r2,  #4            @ 2 * n * sizeof FFTComplex
        lsl             r2,  r2,  #5            @ 4 * n * sizeof FFTComplex
        add             r3,  r2,  r4
        add             r4,  r4,  r0            @ &z[o1]
        add             r2,  r2,  r0            @ &z[o2]
        add             r3,  r3,  r0            @ &z[o3]
        vld1.32         {d20-d21},[r2,:128]     @ {z[o2],z[o2+1]}
        movrel          r12, pmmp
        vld1.32         {d22-d23},[r3,:128]     @ {z[o3],z[o3+1]}
        add             r5,  r5,  r1            @ wim
        vld1.32         {d6-d7},  [r12,:128]    @ pmmp
        vswp            d21, d22
        vld1.32         {d4},     [r1,:64]!     @ {wre[0],wre[1]}
        sub             r5,  r5,  #4            @ wim--
        vrev64.32       q1,  q11
        vmul.f32        q11, q11, d4[1]
        vmul.f32        q1,  q1,  q3
        vld1.32         {d5[0]},  [r5,:32]      @ d5[0] = wim[-1]
        vmla.f32        q11, q1,  d5[0]         @ {t1a,t2a,t5a,t6a}
        vld2.32         {d16-d17},[r0,:128]     @ {z[0],z[1]}
        sub             r6, r6, #1              @ n--
        vld2.32         {d18-d19},[r4,:128]     @ {z[o1],z[o1+1]}
        vzip.32         q10, q11
        vadd.f32        d0,  d22, d20
        vadd.f32        d1,  d21, d23
        vsub.f32        d2,  d21, d23
        vsub.f32        d3,  d22, d20
        vsub.f32        q10, q8,  q0
        vadd.f32        q8,  q8,  q0
        vsub.f32        q11, q9,  q1
        vadd.f32        q9,  q9,  q1
        vst2.32         {d20-d21},[r2,:128]!    @ {z[o2],z[o2+1]}
        vst2.32         {d16-d17},[r0,:128]!    @ {z[0],z[1]}
        vst2.32         {d22-d23},[r3,:128]!    @ {z[o3],z[o3+1]}
        vst2.32         {d18-d19},[r4,:128]!    @ {z[o1],z[o1+1]}
        sub             r5,  r5,  #8            @ wim -= 2
1:
        vld1.32         {d20-d21},[r2,:128]     @ {z[o2],z[o2+1]}
        vld1.32         {d22-d23},[r3,:128]     @ {z[o3],z[o3+1]}
        vswp            d21, d22
        vld1.32         {d4}, [r1]!             @ {wre[0],wre[1]}
        vrev64.32       q0,  q10
        vmul.f32        q10, q10, d4[0]
        vrev64.32       q1,  q11
        vmul.f32        q11, q11, d4[1]
        vld1.32         {d5}, [r5]              @ {wim[-1],wim[0]}
        vmul.f32        q0,  q0,  q3
        sub             r5,  r5,  #8            @ wim -= 2
        vmul.f32        q1,  q1,  q3
        vmla.f32        q10, q0,  d5[1]         @ {t1,t2,t5,t6}
        vmla.f32        q11, q1,  d5[0]         @ {t1a,t2a,t5a,t6a}
        vld2.32         {d16-d17},[r0,:128]     @ {z[0],z[1]}
        subs            r6,  r6,  #1            @ n--
        vld2.32         {d18-d19},[r4,:128]     @ {z[o1],z[o1+1]}
        vzip.32         q10, q11
        vadd.f32        d0,  d22, d20
        vadd.f32        d1,  d21, d23
        vsub.f32        d2,  d21, d23
        vsub.f32        d3,  d22, d20
        vsub.f32        q10, q8,  q0
        vadd.f32        q8,  q8,  q0
        vsub.f32        q11, q9,  q1
        vadd.f32        q9,  q9,  q1
        vst2.32         {d20-d21}, [r2,:128]!   @ {z[o2],z[o2+1]}
        vst2.32         {d16-d17}, [r0,:128]!   @ {z[0],z[1]}
        vst2.32         {d22-d23}, [r3,:128]!   @ {z[o3],z[o3+1]}
        vst2.32         {d18-d19}, [r4,:128]!   @ {z[o1],z[o1+1]}
        bne             1b

        pop             {r4-r6,pc}
endfunc

.macro  def_fft n, n2, n4
        .align 6
function fft\n\()_neon
        push            {r4, lr}
        mov             r4,  r0
        bl              fft\n2\()_neon
        add             r0,  r4,  #\n4*2*8
        bl              fft\n4\()_neon
        add             r0,  r4,  #\n4*3*8
        bl              fft\n4\()_neon
        mov             r0,  r4
        pop             {r4, lr}
        movrel          r1,  X(ff_cos_\n)
        mov             r2,  #\n4/2
        b               fft_pass_neon
endfunc
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

function ff_fft_calc_neon, export=1
        ldr             r2,  [r0]
        sub             r2,  r2,  #2
        movrel          r3,  fft_tab_neon
        ldr             r3,  [r3, r2, lsl #2]
        mov             r0,  r1
        bx              r3
endfunc

function ff_fft_permute_neon, export=1
        push            {r4,lr}
        mov             r12, #1
        ldr             r2,  [r0]       @ nbits
        ldr             r3,  [r0, #12]  @ tmp_buf
        ldr             r0,  [r0, #8]   @ revtab
        lsl             r12, r12, r2
        mov             r2,  r12
1:
        vld1.32         {d0-d1}, [r1,:128]!
        ldr             r4,  [r0], #4
        uxth            lr,  r4
        uxth            r4,  r4,  ror #16
        add             lr,  r3,  lr,  lsl #3
        add             r4,  r3,  r4,  lsl #3
        vst1.32         {d0}, [lr,:64]
        vst1.32         {d1}, [r4,:64]
        subs            r12, r12, #2
        bgt             1b

        sub             r1,  r1,  r2,  lsl #3
1:
        vld1.32         {d0-d3}, [r3,:128]!
        vst1.32         {d0-d3}, [r1,:128]!
        subs            r2,  r2,  #4
        bgt             1b

        pop             {r4,pc}
endfunc

const   fft_tab_neon
        .word fft4_neon
        .word fft8_neon
        .word fft16_neon
        .word fft32_neon
        .word fft64_neon
        .word fft128_neon
        .word fft256_neon
        .word fft512_neon
        .word fft1024_neon
        .word fft2048_neon
        .word fft4096_neon
        .word fft8192_neon
        .word fft16384_neon
        .word fft32768_neon
        .word fft65536_neon
endconst

const   pmmp, align=4
        .float          +1.0, -1.0, -1.0, +1.0
endconst

const   mppm, align=4
        .float          -M_SQRT1_2, M_SQRT1_2, M_SQRT1_2, -M_SQRT1_2
endconst

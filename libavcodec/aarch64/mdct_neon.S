/*
 * AArch64 NEON optimised MDCT
 * Copyright (c) 2009 Mans Rullgard <mans@mansr.com>
 * Copyright (c) 2014 Janne Grunau <janne-libav@jannau.net>
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

#include "libavutil/aarch64/asm.S"

function ff_imdct_half_neon, export=1
        sub             sp,  sp,  #32
        stp             x19, x20, [sp]
        str             x30, [sp, #16]
        mov             x12, #1
        ldr             w14, [x0, #28]          // mdct_bits
        ldr             x4,  [x0, #32]          // tcos
        ldr             x3,  [x0, #8]           // revtab
        lsl             x12, x12, x14           // n  = 1 << nbits
        lsr             x14, x12, #2            // n4 = n >> 2
        add             x7,  x2,  x12,  lsl #1
        mov             x12, #-16
        sub             x7,  x7,  #16

        ld2             {v16.2s,v17.2s}, [x7], x12 // d16=x,n1 d17=x,n0
        ld2             {v0.2s,v1.2s},   [x2], #16 // d0 =m0,x d1 =m1,x
        rev64           v17.2s, v17.2s
        ld2             {v2.2s,v3.2s},   [x4], #16 // d2=c0,c1 d3=s0,s2
        fmul            v6.2s,  v17.2s, v2.2s
        fmul            v7.2s,  v0.2s,  v2.2s
1:
        subs            x14, x14, #2
        ldr             w6,  [x3], #4
        fmul            v4.2s,  v0.2s,  v3.2s
        fmul            v5.2s,  v17.2s, v3.2s
        fsub            v4.2s,  v6.2s,  v4.2s
        fadd            v5.2s,  v5.2s,  v7.2s
        ubfm            x8,  x6,  #16, #31
        ubfm            x6,  x6,  #0,  #15
        add             x8,  x1,  x8,  lsl #3
        add             x6,  x1,  x6,  lsl #3
        b.eq            2f
        ld2             {v16.2s,v17.2s}, [x7], x12
        ld2             {v0.2s,v1.2s},   [x2], #16
        rev64           v17.2s, v17.2s
        ld2             {v2.2s,v3.2s},   [x4], #16    // d2=c0,c1 d3=s0,s2
        fmul            v6.2s,  v17.2s, v2.2s
        fmul            v7.2s,  v0.2s,  v2.2s
        st2             {v4.s,v5.s}[0], [x6]
        st2             {v4.s,v5.s}[1], [x8]
        b               1b
2:
        st2             {v4.s,v5.s}[0], [x6]
        st2             {v4.s,v5.s}[1], [x8]

        mov             x19, x0
        mov             x20, x1
        bl              X(ff_fft_calc_neon)

        mov             x12, #1
        ldr             w14, [x19, #28]          // mdct_bits
        ldr             x4,  [x19, #32]          // tcos
        lsl             x12, x12, x14            // n  = 1 << nbits
        lsr             x14, x12, #3             // n8 = n >> 3

        add             x4,  x4,  x14, lsl #3
        add             x6,  x20, x14, lsl #3
        sub             x1,  x4,  #16
        sub             x3,  x6,  #16

        mov             x7,  #-16
        mov             x8,  x6
        mov             x0,  x3

        ld2             {v0.2s,v1.2s},  [x3], x7 // d0 =i1,r1 d1 =i0,r0
        ld2             {v20.2s,v21.2s},[x6], #16 // d20=i2,r2 d21=i3,r3
        ld2             {v16.2s,v17.2s},[x1], x7 // d16=c1,c0 d18=s1,s0
3:
        subs            x14, x14, #2
        fmul            v7.2s,  v0.2s,  v17.2s
        ld2             {v18.2s,v19.2s},[x4], #16    // d17=c2,c3 d19=s2,s3
        fmul            v4.2s,  v1.2s,  v17.2s
        fmul            v6.2s,  v21.2s, v19.2s
        fmul            v5.2s,  v20.2s, v19.2s
        fmul            v22.2s, v1.2s,  v16.2s
        fmul            v23.2s, v21.2s, v18.2s
        fmul            v24.2s, v0.2s,  v16.2s
        fmul            v25.2s, v20.2s, v18.2s
        fadd            v7.2s,  v7.2s,  v22.2s
        fadd            v5.2s,  v5.2s,  v23.2s
        fsub            v4.2s,  v4.2s,  v24.2s
        fsub            v6.2s,  v6.2s,  v25.2s
        b.eq            4f
        ld2             {v0.2s,v1.2s},  [x3], x7
        ld2             {v20.2s,v21.2s},[x6], #16
        ld2             {v16.2s,v17.2s},[x1], x7 // d16=c1,c0 d18=s1,s0
        rev64           v5.2s,  v5.2s
        rev64           v7.2s,  v7.2s
        st2             {v4.2s,v5.2s},  [x0], x7
        st2             {v6.2s,v7.2s},  [x8], #16
        b               3b
4:
        rev64           v5.2s,  v5.2s
        rev64           v7.2s,  v7.2s
        st2             {v4.2s,v5.2s},  [x0]
        st2             {v6.2s,v7.2s},  [x8]

        ldp             x19, x20, [sp]
        ldr             x30, [sp, #16]
        add             sp,  sp,  #32

        ret
endfunc

function ff_imdct_calc_neon, export=1
        sub             sp,  sp,  #32
        stp             x19, x20, [sp]
        str             x30, [sp, #16]
        ldr             w3,  [x0, #28]          // mdct_bits
        mov             x19, #1
        mov             x20, x1
        lsl             x19, x19, x3
        add             x1,  x1,  x19

        bl              X(ff_imdct_half_neon)

        add             x0,  x20, x19,  lsl #2
        add             x1,  x20, x19,  lsl #1
        sub             x0,  x0,  #8
        sub             x2,  x1,  #16
        mov             x3,  #-16
        mov             x6,  #-8
1:
        ld1             {v0.4s}, [x2], x3
        prfum           pldl1keep, [x0, #-16]
        rev64           v0.4s, v0.4s
        ld1             {v2.2s,v3.2s}, [x1], #16
        fneg            v4.4s,  v0.4s
        prfum           pldl1keep, [x2, #-16]
        rev64           v2.2s, v2.2s
        rev64           v3.2s, v3.2s
        ext             v4.16b, v4.16b, v4.16b, #8
        st1             {v2.2s}, [x0], x6
        st1             {v3.2s}, [x0], x6
        st1             {v4.4s}, [x20], #16
        subs            x19, x19,  #16
        b.gt            1b

        ldp             x19, x20, [sp], #16
        ldr             x30, [sp], #16

        ret
endfunc


function ff_mdct_calc_neon, export=1
        sub             sp,  sp,  #32
        stp             x19, x20, [sp]
        str             x30, [sp, #16]

        mov             x12, #1
        ldr             w14, [x0, #28]          // mdct_bits
        ldr             x4,  [x0, #32]          // tcos
        ldr             x3,  [x0, #8]           // revtab
        lsl             x14, x12, x14           // n  = 1 << nbits
        add             x7,  x2,  x14           // in4u
        sub             x9,  x7,  #16           // in4d
        add             x2,  x7,  x14, lsl #1   // in3u
        add             x8,  x9,  x14, lsl #1   // in3d
        add             x5,  x4,  x14, lsl #1
        sub             x5,  x5,  #16
        sub             x3,  x3,  #4
        mov             x12, #-16
        lsr             x13, x14, #1

        ld2             {v16.2s,v17.2s}, [x9], x12  // in0u0,in0u1 in4d1,in4d0
        ld2             {v18.2s,v19.2s}, [x8], x12  // in2u0,in2u1 in3d1,in3d0
        ld2             {v0.2s, v1.2s},  [x7], #16  // in4u0,in4u1 in2d1,in2d0
        rev64           v17.2s, v17.2s              // in4d0,in4d1 in3d0,in3d1
        rev64           v19.2s, v19.2s              // in4d0,in4d1 in3d0,in3d1
        ld2             {v2.2s, v3.2s},  [x2], #16  // in3u0,in3u1 in1d1,in1d0
        fsub            v0.2s,  v17.2s, v0.2s       // in4d-in4u      I
        ld2             {v20.2s,v21.2s}, [x4], #16  // c0,c1 s0,s1
        rev64           v1.2s,  v1.2s               // in2d0,in2d1 in1d0,in1d1
        rev64           v3.2s,  v3.2s               // in2d0,in2d1 in1d0,in1d1
        ld2             {v30.2s,v31.2s}, [x5], x12  // c2,c3 s2,s3
        fadd            v2.2s,  v2.2s,  v19.2s      // in3u+in3d     -R
        fsub            v16.2s, v16.2s, v1.2s       // in0u-in2d      R
        fadd            v18.2s, v18.2s, v3.2s       // in2u+in1d     -I
1:
        fmul            v7.2s,  v0.2s,  v21.2s      //  I*s
        ldr             w10, [x3, x13]
        fmul            v6.2s,  v2.2s,  v20.2s      // -R*c
        ldr             w6,  [x3, #4]!
        fmul            v4.2s,  v2.2s,  v21.2s      // -R*s
        fmul            v5.2s,  v0.2s,  v20.2s      //  I*c
        fmul            v24.2s, v16.2s, v30.2s      //  R*c
        fmul            v25.2s, v18.2s, v31.2s      // -I*s
        fmul            v22.2s, v16.2s, v31.2s      //  R*s
        fmul            v23.2s, v18.2s, v30.2s      //  I*c
        subs            x14, x14, #16
        subs            x13, x13, #8
        fsub            v6.2s,  v6.2s,  v7.2s       // -R*c-I*s
        fadd            v7.2s,  v4.2s,  v5.2s       // -R*s+I*c
        fsub            v24.2s, v25.2s, v24.2s      // I*s-R*c
        fadd            v25.2s, v22.2s, v23.2s      // R*s-I*c
        b.eq            1f
        mov             x12, #-16
        ld2             {v16.2s,v17.2s}, [x9], x12  // in0u0,in0u1 in4d1,in4d0
        ld2             {v18.2s,v19.2s}, [x8], x12  // in2u0,in2u1 in3d1,in3d0
        fneg            v7.2s,  v7.2s               //  R*s-I*c
        ld2             {v0.2s, v1.2s},  [x7], #16  // in4u0,in4u1 in2d1,in2d0
        rev64           v17.2s, v17.2s              // in4d0,in4d1 in3d0,in3d1
        rev64           v19.2s, v19.2s              // in4d0,in4d1 in3d0,in3d1
        ld2             {v2.2s, v3.2s},  [x2], #16  // in3u0,in3u1 in1d1,in1d0
        fsub            v0.2s,  v17.2s, v0.2s       // in4d-in4u      I
        ld2             {v20.2s,v21.2s}, [x4], #16  // c0,c1 s0,s1
        rev64           v1.2s,  v1.2s               // in2d0,in2d1 in1d0,in1d1
        rev64           v3.2s,  v3.2s               // in2d0,in2d1 in1d0,in1d1
        ld2             {v30.2s,v31.2s}, [x5], x12  // c2,c3 s2,s3
        fadd            v2.2s,  v2.2s,  v19.2s      // in3u+in3d     -R
        fsub            v16.2s, v16.2s, v1.2s       // in0u-in2d      R
        fadd            v18.2s, v18.2s, v3.2s       // in2u+in1d     -I
        ubfm            x12, x6,  #16, #31
        ubfm            x6,  x6,  #0,  #15
        add             x12, x1,  x12, lsl #3
        add             x6,  x1,  x6,  lsl #3
        st2             {v6.s,v7.s}[0],   [x6]
        st2             {v6.s,v7.s}[1],   [x12]
        ubfm            x6,  x10, #16, #31
        ubfm            x10, x10, #0,  #15
        add             x6 , x1,  x6,  lsl #3
        add             x10, x1,  x10, lsl #3
        st2             {v24.s,v25.s}[0], [x10]
        st2             {v24.s,v25.s}[1], [x6]
        b               1b
1:
        fneg            v7.2s,  v7.2s           //  R*s-I*c
        ubfm            x12, x6,  #16, #31
        ubfm            x6,  x6,  #0,  #15
        add             x12, x1,  x12, lsl #3
        add             x6,  x1,  x6,  lsl #3
        st2             {v6.s,v7.s}[0],   [x6]
        st2             {v6.s,v7.s}[1],   [x12]
        ubfm            x6,  x10, #16, #31
        ubfm            x10, x10, #0,  #15
        add             x6 , x1,  x6,  lsl #3
        add             x10, x1,  x10, lsl #3
        st2             {v24.s,v25.s}[0], [x10]
        st2             {v24.s,v25.s}[1], [x6]

        mov             x19, x0
        mov             x20, x1
        bl              X(ff_fft_calc_neon)

        mov             x12, #1
        ldr             w14, [x19, #28]         // mdct_bits
        ldr             x4,  [x19, #32]         // tcos
        lsl             x12, x12, x14           // n  = 1 << nbits
        lsr             x14, x12, #3            // n8 = n >> 3

        add             x4,  x4,  x14, lsl #3
        add             x6,  x20, x14, lsl #3
        sub             x1,  x4,  #16
        sub             x3,  x6,  #16

        mov             x7,  #-16
        mov             x8,  x6
        mov             x0,  x3

        ld2             {v0.2s,v1.2s},   [x3], x7   // d0 =r1,i1 d1 =r0,i0
        ld2             {v20.2s,v21.2s}, [x6], #16  // d20=r2,i2 d21=r3,i3
        ld2             {v16.2s,v17.2s}, [x1], x7   // c1,c0 s1,s0
1:
        subs            x14, x14, #2
        fmul            v7.2s,  v0.2s,  v17.2s      // r1*s1,r0*s0
        ld2             {v18.2s,v19.2s}, [x4], #16  // c2,c3 s2,s3
        fmul            v4.2s,  v1.2s,  v17.2s      // i1*s1,i0*s0
        fmul            v6.2s,  v21.2s, v19.2s      // i2*s2,i3*s3
        fmul            v5.2s,  v20.2s, v19.2s      // r2*s2,r3*s3
        fmul            v24.2s, v0.2s,  v16.2s      // r1*c1,r0*c0
        fmul            v25.2s, v20.2s, v18.2s      // r2*c2,r3*c3
        fmul            v22.2s, v21.2s, v18.2s      // i2*c2,i3*c3
        fmul            v23.2s, v1.2s,  v16.2s      // i1*c1,i0*c0
        fadd            v4.2s,  v4.2s,  v24.2s      // i1*s1+r1*c1,i0*s0+r0*c0
        fadd            v6.2s,  v6.2s,  v25.2s      // i2*s2+r2*c2,i3*s3+r3*c3
        fsub            v5.2s,  v22.2s, v5.2s       // i2*c2-r2*s2,i3*c3-r3*s3
        fsub            v7.2s,  v23.2s, v7.2s       // i1*c1-r1*s1,i0*c0-r0*s0
        fneg            v4.2s,  v4.2s
        fneg            v6.2s,  v6.2s
        b.eq            1f
        ld2             {v0.2s, v1.2s},  [x3], x7
        ld2             {v20.2s,v21.2s}, [x6], #16
        ld2             {v16.2s,v17.2s}, [x1], x7   // c1,c0 s1,s0
        rev64           v5.2s,  v5.2s
        rev64           v7.2s,  v7.2s
        st2             {v4.2s,v5.2s},  [x0], x7
        st2             {v6.2s,v7.2s},  [x8], #16
        b               1b
1:
        rev64           v5.2s,  v5.2s
        rev64           v7.2s,  v7.2s
        st2             {v4.2s,v5.2s},  [x0]
        st2             {v6.2s,v7.2s},  [x8]

        ldp             x19, x20, [sp], #16
        ldr             x30, [sp], #16
        ret
endfunc

/*
 * Copyright (c) 2014 Janne Grunau <janne-libav@jannau.net>
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

#include "libavutil/aarch64/asm.S"

#include "asm-offsets.h"

.macro shuffle a, b, c, d
const shuffle_\a\b\c\d, align=4
        .byte (\a * 4), (\a * 4 + 1), (\a * 4 + 2), (\a * 4 + 3)
        .byte (\b * 4), (\b * 4 + 1), (\b * 4 + 2), (\b * 4 + 3)
        .byte (\c * 4), (\c * 4 + 1), (\c * 4 + 2), (\c * 4 + 3)
        .byte (\d * 4), (\d * 4 + 1), (\d * 4 + 2), (\d * 4 + 3)
endconst
.endm

shuffle 0, 2, 1, 3
shuffle 1, 0, 3, 2
shuffle 2, 3, 0, 1
shuffle 3, 1, 2, 0


function fft5_neon
        lsl             x2,  x2,  #3
        ld1             {v24.2s},         [x1],  x2
        ld2             {v25.s,v26.s}[0], [x1],  x2
        ld2             {v25.s,v26.s}[1], [x1],  x2
        ld2             {v25.s,v26.s}[2], [x1],  x2
        ld2             {v25.s,v26.s}[3], [x1]
        dup             v6.4s,  v24.s[0]
        dup             v7.4s,  v24.s[1]

        faddp           v0.4s,  v25.4s, v26.4s
        // z[][0], z[][3]
        fmul            v16.4s, v25.4s, v15.s[0] // rr
        fmul            v17.4s, v25.4s, v15.s[1] // ri
        fmul            v18.4s, v26.4s, v15.s[0] // ir
        fmul            v19.4s, v26.4s, v15.s[1] // ii
        faddp           v0.4s,  v0.4s,  v0.4s
        // z[][1], z[][2]
        fmul            v20.4s, v25.4s, v15.s[2] // rr
        fmul            v21.4s, v25.4s, v15.s[3] // ri
        fmul            v22.4s, v26.4s, v15.s[2] // ir
        fmul            v23.4s, v26.4s, v15.s[3] // ii
        fadd            v0.2s,  v24.2s, v0.2s   // out[0]

        // z[0123][0], z[0123][3]
        fsub            v24.4s, v16.4s, v19.4s  //    (c).re =  rr - ii;
        fadd            v27.4s, v16.4s, v19.4s  //    (d).re =  rr + ii;
        ld1             {v16.16b},  [x11]
        ld1             {v19.16b},  [x14]
        fadd            v28.4s, v17.4s, v18.4s  //    (c).im =  ri + ir;
        fsub            v31.4s, v18.4s, v17.4s  //    (d).im = -ri + ir;
        ld1             {v17.16b},  [x12]
        // z[0123][1], z[0123][2]
        fsub            v25.4s, v20.4s, v23.4s  //    (c).re =  rr - ii;
        fadd            v26.4s, v20.4s, v23.4s  //    (d).re =  rr + ii;
        ld1             {v18.16b},  [x13]
        fadd            v29.4s, v21.4s, v22.4s  //    (c).im =  ri + ir;
        fsub            v30.4s, v22.4s, v21.4s  //    (d).im = -ri + ir;

        //real
        tbl             v20.16b, {v24.16b}, v16.16b
        tbl             v21.16b, {v25.16b}, v17.16b
        tbl             v22.16b, {v26.16b}, v18.16b
        tbl             v23.16b, {v27.16b}, v19.16b
        //imag
        tbl             v16.16b, {v28.16b}, v16.16b
        tbl             v17.16b, {v29.16b}, v17.16b
        tbl             v18.16b, {v30.16b}, v18.16b
        tbl             v19.16b, {v31.16b}, v19.16b

        fadd            v6.4s,  v6.4s,  v20.4s
        fadd            v22.4s, v22.4s, v23.4s
        fadd            v7.4s,  v7.4s,  v16.4s
        fadd            v18.4s, v18.4s, v19.4s

        fadd            v21.4s, v21.4s, v22.4s
        fadd            v17.4s, v17.4s, v18.4s
        fadd            v6.4s,  v6.4s,  v21.4s
        fadd            v7.4s,  v7.4s,  v17.4s

        ret
endfunc

function fft15_neon
        mov             x8,  x1
        mov             x9,  x30
        add             x2,  x3,  x3,  lsl #1   // 3 * stride

        add             x1,  x8,  x3,  lsl #3   // in + 1 * stride
        bl              fft5_neon
        mov             v1.8b,   v0.8b
        mov             v2.16b,  v6.16b
        mov             v3.16b,  v7.16b

        add             x1,  x8,  x3,  lsl #4   // in + 2 * stride
        add             x2,  x3,  x3,  lsl #1   // 3 * stride
        bl              fft5_neon
        zip1            v1.4s,   v1.4s,  v0.4s
        mov             v4.16b,  v6.16b
        mov             v5.16b,  v7.16b

        mov             x1,  x8                 // in + 0 * stride
        add             x2,  x3,  x3,  lsl #1   // 3 * stride
        bl              fft5_neon

        faddp           v20.4s, v1.4s,  v1.4s

        ext             v18.16b, v8.16b,  v8.16b,  #4
        ext             v19.16b, v9.16b,  v9.16b,  #4
        mov             v16.16b, v6.16b
        mov             v17.16b, v7.16b
        fadd            v20.2s, v20.2s, v0.2s

        uzp1            v18.4s, v18.4s, v10.4s  // exp[2,4,6,8].re
        uzp1            v19.4s, v19.4s, v11.4s  // exp[2,4,6,8].im

        st1             {v20.2s},  [x0], #8     // out[0]

        fmla            v16.4s, v2.4s,  v8.4s
        fmls            v16.4s, v3.4s,  v9.4s

        fmla            v17.4s, v2.4s,  v9.4s
        fmla            v17.4s, v3.4s,  v8.4s

        fmla            v16.4s, v4.4s,  v18.4s
        fmls            v16.4s, v5.4s,  v19.4s

        fmla            v17.4s, v4.4s,  v19.4s
        fmla            v17.4s, v5.4s,  v18.4s

        zip1            v18.4s, v16.4s, v17.4s
        zip2            v19.4s, v16.4s, v17.4s

        rev64           v31.4s, v14.4s
        trn1            v28.2d, v1.2d,  v1.2d
        trn2            v29.2d, v1.2d,  v1.2d
        zip1            v30.2d, v14.2d, v31.2d
        zip2            v31.2d, v14.2d, v31.2d

        st1             {v18.4s,v19.4s},  [x0], #32 // out[1-4]

        fmul            v16.4s, v28.4s, v30.4s
        fmul            v17.4s, v29.4s, v30.4s
        fmls            v16.4s, v29.4s, v31.4s
        fmla            v17.4s, v28.4s, v31.4s
        faddp           v16.4s, v16.4s, v16.4s
        faddp           v17.4s, v17.4s, v17.4s
        zip1            v18.2s, v16.2s, v17.2s
        zip2            v19.2s, v16.2s, v17.2s

        fadd            v18.2s, v18.2s, v0.2s
        fadd            v0.2s,  v19.2s, v0.2s

        ext             v30.16b, v12.16b, v12.16b, #4
        ext             v31.16b, v13.16b, v13.16b, #4
        mov             v16.16b, v6.16b
        mov             v17.16b, v7.16b

        uzp1            v30.4s, v30.4s, v8.4s
        uzp1            v31.4s, v31.4s, v9.4s

        st1             {v18.2s},  [x0], #8     // out[5]

        fmla            v16.4s, v2.4s,  v10.4s
        fmls            v16.4s, v3.4s,  v11.4s

        fmla            v17.4s, v2.4s,  v11.4s
        fmla            v17.4s, v3.4s,  v10.4s

        fmla            v16.4s, v4.4s,  v30.4s
        fmls            v16.4s, v5.4s,  v31.4s

        fmla            v17.4s, v4.4s,  v31.4s
        fmla            v17.4s, v5.4s,  v30.4s

        zip1            v18.4s, v16.4s, v17.4s
        zip2            v19.4s, v16.4s, v17.4s

        ext             v30.16b, v10.16b, v10.16b, #4
        ext             v31.16b, v11.16b, v11.16b, #4

        fmla            v6.4s,  v2.4s,  v12.4s
        fmls            v6.4s,  v3.4s,  v13.4s

        st1             {v18.4s,v19.4s},  [x0], #32 // out[6-9]

        uzp1            v30.4s, v30.4s, v12.4s
        uzp1            v31.4s, v31.4s, v13.4s

        fmla            v7.4s,  v2.4s,  v13.4s
        fmla            v7.4s,  v3.4s,  v12.4s

        st1             {v0.2s},  [x0], #8     // out[10]

        fmla            v6.4s,  v4.4s,  v30.4s
        fmls            v6.4s,  v5.4s,  v31.4s

        fmla            v7.4s,  v4.4s,  v31.4s
        fmla            v7.4s,  v5.4s,  v30.4s

        zip1            v18.4s, v6.4s,  v7.4s
        zip2            v19.4s, v6.4s,  v7.4s

        st1             {v18.4s,v19.4s},  [x0], #32 // out[11-14]

        ret             x9
endfunc

// x0: out, x1: out+len2, x2: exptab, x3: len2
function fft15_pass
        ands            x6,  x3,  #3
        mov             x4,  x0
        mov             x5,  x1
        b.eq            9f
        ld1             {v0.2s},  [x0], #8
        ld1             {v1.2s},  [x1], #8
        sub             x3,  x3,  x6
        subs            x6,  x6,  #1
        fadd            v2.2s,  v0.2s,  v1.2s
        fsub            v3.2s,  v0.2s,  v1.2s
        add             x2,  x2,  #8
        st1             {v2.2s},  [x4], #8
        st1             {v3.2s},  [x5], #8
        b.eq            9f
1:
        subs            x6,  x6,  #1
        ldp             s4,  s5,  [x2], #8
        ldp             s2,  s3,  [x1], #8
        ldp             s0,  s1,  [x0], #8

        fmul            s6,  s2,  s4
        fmul            s7,  s2,  s5
        fmls            s6,  s3,  v5.s[0]
        fmla            s7,  s3,  v4.s[0]

        fsub            s2,  s0,  s6
        fsub            s3,  s1,  s7
        fadd            s0,  s0,  s6
        fadd            s1,  s1,  s7

        stp             s2,  s3,  [x5], #8
        stp             s0,  s1,  [x4], #8
        b.gt            1b
9:
        ld1             {v4.4s,v5.4s}, [x2],  #32
        ld2             {v2.4s,v3.4s}, [x1],  #32
        uzp1            v6.4s,  v4.4s,  v5.4s
        uzp2            v7.4s,  v4.4s,  v5.4s
        ld2             {v0.4s,v1.4s}, [x0],  #32
8:
        subs            x3,  x3,  #8

        fmul            v4.4s,  v2.4s,  v6.4s
        fmul            v5.4s,  v2.4s,  v7.4s
        b.lt            4f

        ld1             {v18.4s,v19.4s}, [x2],  #32

        fmls            v4.4s,  v3.4s,  v7.4s
        fmla            v5.4s,  v3.4s,  v6.4s

        ld2             {v22.4s,v23.4s}, [x1],  #32

        fsub            v2.4s,  v0.4s,  v4.4s
        fadd            v0.4s,  v0.4s,  v4.4s
        fsub            v3.4s,  v1.4s,  v5.4s
        fadd            v1.4s,  v1.4s,  v5.4s

        uzp1            v16.4s, v18.4s, v19.4s
        uzp2            v17.4s, v18.4s, v19.4s

        st2             {v2.4s,v3.4s}, [x5],  #32
        st2             {v0.4s,v1.4s}, [x4],  #32
        ld2             {v20.4s,v21.4s}, [x0],  #32

        fmul            v18.4s, v22.4s, v16.4s
        fmul            v19.4s, v22.4s, v17.4s
        b.eq            0f

        ld1             {v4.4s,v5.4s}, [x2],  #32

        fmls            v18.4s, v23.4s, v17.4s
        fmla            v19.4s, v23.4s, v16.4s

        ld2             {v2.4s,v3.4s}, [x1],  #32

        fsub            v22.4s, v20.4s, v18.4s
        fadd            v20.4s, v20.4s, v18.4s
        fsub            v23.4s, v21.4s, v19.4s
        fadd            v21.4s, v21.4s, v19.4s

        uzp1            v6.4s,  v4.4s,  v5.4s
        uzp2            v7.4s,  v4.4s,  v5.4s

        st2             {v22.4s,v23.4s}, [x5],  #32
        st2             {v20.4s,v21.4s}, [x4],  #32
        ld2             {v0.4s,v1.4s}, [x0],  #32

        b               8b
4:
        fmls            v4.4s,  v3.4s,  v7.4s
        fmla            v5.4s,  v3.4s,  v6.4s

        fsub            v2.4s,  v0.4s,  v4.4s
        fadd            v0.4s,  v0.4s,  v4.4s
        fsub            v3.4s,  v1.4s,  v5.4s
        fadd            v1.4s,  v1.4s,  v5.4s

        st2             {v2.4s,v3.4s}, [x5],  #32
        st2             {v0.4s,v1.4s}, [x4],  #32

        ret
0:
        fmls            v18.4s, v23.4s, v17.4s
        fmla            v19.4s, v23.4s, v16.4s

        fsub            v22.4s, v20.4s, v18.4s
        fadd            v20.4s, v20.4s, v18.4s
        fsub            v23.4s, v21.4s, v19.4s
        fadd            v21.4s, v21.4s, v19.4s

        st2             {v22.4s,v23.4s}, [x5],  #32
        st2             {v20.4s,v21.4s}, [x4],  #32

        ret
endfunc

function fft30_neon, align=6
        sub             sp,  sp,  #0x20
        stp             x20, x21, [sp]
        stp             x22, x30, [sp, #0x10]
        mov             x21, x1
        mov             x22, x2
        mov             x20, x4
        mov             x0,  x21
        mov             x1,  x22
        lsl             x3,  x20, #1
        bl              fft15_neon

        add             x0,  x21, #15*8
        add             x1,  x22, x20,  lsl #3
        lsl             x3,  x20, #1
        bl              fft15_neon

        ldr             x2,  [x10, #(CELT_EXPTAB + 8)]  // s->exptab[1]
        add             x0,  x21, #0
        add             x1,  x21, #15*8
        mov             x3,  #15
        ldp             x20, x21, [sp]
        ldp             x22, x30, [sp, #0x10]
        add             sp,  sp,  #0x20
        b               fft15_pass
endfunc

.macro  def_fft n, n2
function fft\n\()_neon, align=6
        sub             sp,  sp,  #0x30
        stp             x20, x21, [sp]
        stp             x22, x30, [sp, #0x10]
        stp             x23, x24, [sp, #0x20]
        mov             x21, x1
        mov             x22, x2
        mov             x23, x3
        mov             x20, x4
        sub             x3,  x3,  #1
        lsl             x4,  x4,  #1
        bl              fft\n2\()_neon

        add             x1,  x21, #(\n2 * 8)
        add             x2,  x22, x20, lsl #3
        sub             x3,  x23, #1
        lsl             x4,  x20, #1
        bl              fft\n2\()_neon

        add             x5,  x10, #CELT_EXPTAB
        mov             x0,  x21
        ldr             x2,  [x5,  x23, lsl #3] // s->exptab[N]
        add             x1,  x21, #(\n2 * 8)
        mov             x3,  #\n2
        ldp             x20, x21, [sp]
        ldp             x22, x30, [sp, #0x10]
        ldp             x23, x24, [sp, #0x20]
        add             sp,  sp,  #0x30
        b               fft15_pass
endfunc
.endm

        def_fft    60,  30
        def_fft   120,  60
        def_fft   240, 120
        def_fft   480, 240
        def_fft   960, 480

function fft_b15_calc_neon
        sub             sp,  sp,  #0x50
        ldr             x8,  [x0,  #CELT_EXPTAB]    // s->exptab[0]
        movrel          x6,  fact5
        movrel          x11, shuffle_0213
        movrel          x12, shuffle_1032
        movrel          x13, shuffle_2301
        movrel          x14, shuffle_3120
        add             x8,  x8,  #8
        movrel          x5,  fft_tab_neon
        stp             x20, x30, [sp]
        stp             d8,  d9,  [sp, #0x10]
        stp             d10, d11, [sp, #0x20]
        stp             d12, d13, [sp, #0x30]
        stp             d14, d15, [sp, #0x40]
        ld1             {v15.4s}, [x6]
        ld1             {v0.4s,v1.4s},   [x8],  #32
        ld1             {v6.2s},  [x8],  #8
        ld1             {v2.4s,v3.4s},   [x8],  #32
        ld1             {v7.2s},  [x8],  #8
        ld1             {v4.4s,v5.4s},   [x8],  #32
        uzp1            v8.4s,  v0.4s,  v1.4s   // exp[ 1 -  4].re
        uzp2            v9.4s,  v0.4s,  v1.4s   // exp[ 1 -  4].im
        uzp1            v10.4s, v2.4s,  v3.4s   // exp[ 6 -  9].re
        uzp2            v11.4s, v2.4s,  v3.4s   // exp[ 6 -  9].im
        uzp1            v12.4s, v4.4s,  v5.4s   // exp[11 - 14].re
        uzp2            v13.4s, v4.4s,  v5.4s   // exp[11 - 14].im
        zip1            v14.4s, v6.4s,  v7.4s   // exp[5,10].re/exp[5,10].im
        ldr             x6,  [x5, x3, lsl #3]
        add             x5,  x5,  x6
        mov             x10, x0
        blr             x5
        ldp             x20, x30, [sp]
        ldp             d8,  d9,  [sp, #0x10]
        ldp             d10, d11, [sp, #0x20]
        ldp             d12, d13, [sp, #0x30]
        ldp             d14, d15, [sp, #0x40]
        add             sp,  sp,  #0x50
        ret
endfunc

const   fft_tab_neon
        .quad fft15_neon  - fft_tab_neon
        .quad fft30_neon  - fft_tab_neon
        .quad fft60_neon  - fft_tab_neon
        .quad fft120_neon - fft_tab_neon
        .quad fft240_neon - fft_tab_neon
        .quad fft480_neon - fft_tab_neon
        .quad fft960_neon - fft_tab_neon
endconst

function ff_celt_imdct_half_neon, export=1
        sub             sp,  sp,  #0x20
        stp             x21, x30, [sp]
        str             s0, [sp, #0x10]

        ldp             w5,  w6,  [x0,  #CELT_LEN2] // CELT_LEN4
        mov             x10, x0
        mov             x21, x1
        sub             w5,  w5,  #1
        lsl             x7,  x3,  #3            //  2 * stride * sizeof(float)
        sub             x8,  xzr, x3,  lsl #3   // -2 * stride * sizeof(float)
        mul             x5,  x5,  x3
        ldp             x9,  x10, [x0,  #CELT_TMP]  // CELT_TWIDDLE
        ldr             w3,  [x0, #CELT_FFT_N]
        add             x5,  x2,  x5,  lsl #2
        mov             x11, x9

        sub             w6,  w6,  #4
        ld1             {v0.s}[0],  [x5], x8
        ld1             {v1.s}[0],  [x2], x7
        ld1             {v4.4s,v5.4s}, [x10], #32
        ld1             {v0.s}[1],  [x5], x8
        ld1             {v1.s}[1],  [x2], x7
        uzp1            v2.4s,  v4.4s,  v5.4s
        ld1             {v0.s}[2],  [x5], x8
        ld1             {v1.s}[2],  [x2], x7
        uzp2            v3.4s,  v4.4s,  v5.4s
        ld1             {v0.s}[3],  [x5], x8
        ld1             {v1.s}[3],  [x2], x7
1:
        subs            w6,  w6,  #4

        ld1             {v20.s}[0], [x5], x8
        ld1             {v21.s}[0], [x2], x7
        ld1             {v4.4s,v5.4s}, [x10], #32

        fmul            v6.4s,  v0.4s,  v2.4s
        fmul            v7.4s,  v0.4s,  v3.4s

        ld1             {v20.s}[1], [x5], x8
        ld1             {v21.s}[1], [x2], x7

        fmls            v6.4s,  v1.4s,  v3.4s
        fmla            v7.4s,  v1.4s,  v2.4s

        ld1             {v20.s}[2], [x5], x8
        ld1             {v21.s}[2], [x2], x7

        uzp1            v2.4s,  v4.4s,  v5.4s
        uzp2            v3.4s,  v4.4s,  v5.4s
        ld1             {v20.s}[3], [x5], x8
        ld1             {v21.s}[3], [x2], x7

        zip1            v4.4s,  v6.4s,  v7.4s
        zip2            v5.4s,  v6.4s,  v7.4s

        fmul            v6.4s,  v20.4s, v2.4s
        fmul            v7.4s,  v20.4s, v3.4s

        st1             {v4.4s,v5.4s}, [x9], #32

        fmls            v6.4s,  v21.4s, v3.4s
        fmla            v7.4s,  v21.4s, v2.4s

        b.eq            3f

        subs            w6,  w6,  #4
        ld1             {v4.4s,v5.4s}, [x10], #32
        ld1             {v0.s}[0],  [x5], x8
        ld1             {v1.s}[0],  [x2], x7
        uzp1            v2.4s,  v4.4s,  v5.4s
        ld1             {v0.s}[1],  [x5], x8
        ld1             {v1.s}[1],  [x2], x7
        uzp2            v3.4s,  v4.4s,  v5.4s
        ld1             {v0.s}[2],  [x5], x8
        ld1             {v1.s}[2],  [x2], x7
        zip1            v4.4s,  v6.4s,  v7.4s
        zip2            v5.4s,  v6.4s,  v7.4s
        ld1             {v0.s}[3],  [x5], x8
        ld1             {v1.s}[3],  [x2], x7

        st1             {v4.4s,v5.4s}, [x9], #32

        b.gt            1b

        fmul            v6.4s,  v0.4s,  v2.4s
        fmul            v7.4s,  v0.4s,  v3.4s
        fmls            v6.4s,  v1.4s,  v3.4s
        fmla            v7.4s,  v1.4s,  v2.4s
3:
        zip1            v4.4s,  v6.4s,  v7.4s
        zip2            v5.4s,  v6.4s,  v7.4s
        st1             {v4.4s,v5.4s}, [x9], #32

        mov             x2,  x11
        mov             x4,  #1

        bl              fft_b15_calc_neon

        ldr             w5,  [x10, #CELT_LEN4]
        ldr             x6,  [x10, #CELT_TWIDDLE]
        ldr             s31, [sp, #0x10]

        add             x1,  x21, x5,  lsl #2
        add             x3,  x6,  x5,  lsl #2
        sub             x0,  x1,  #16
        sub             x2,  x3,  #16
        mov             x8,  #-16
        mov             x7,  #16
        mov             x10, x0
        mov             x11, x1

        sub             w5,  w5,  #4

        ld1             {v0.4s},  [x0], x8
        ld1             {v1.4s},  [x1], x7
        ld1             {v2.4s},  [x2], x8
        ld1             {v3.4s},  [x3], x7

        uzp1            v4.4s,  v0.4s,  v1.4s   // z[-i-2, -i-1, +i, i+1].re
        uzp2            v6.4s,  v0.4s,  v1.4s   // z[-i-2, -i-1, +i, i+1].im

        uzp1            v5.4s,  v2.4s,  v3.4s   // twidlle_exptab[-i-2, -i-1, +i, i+1].re
        uzp2            v7.4s,  v2.4s,  v3.4s   // twidlle_exptab[-i-2, -i-1, +i, i+1].im

        fmul            v1.4s,  v6.4s,  v5.4s
        fmul            v0.4s,  v6.4s,  v7.4s
2:
        subs            w5,  w5,  #4

        ld1             {v20.4s}, [x0], x8

        fmla            v1.4s,  v4.4s,  v7.4s
        fmls            v0.4s,  v4.4s,  v5.4s

        ld1             {v21.4s}, [x1], x7

        ext             v1.16b, v1.16b, v1.16b, #8
        fmul            v0.4s,  v0.4s,  v31.s[0]

        ld1             {v2.4s},  [x2], x8

        rev64           v1.4s,  v1.4s
        fmul            v1.4s,  v1.4s,  v31.s[0]

        ld1             {v3.4s},  [x3], x7

        zip1            v5.4s,  v0.4s,  v1.4s
        zip2            v7.4s,  v0.4s,  v1.4s

        uzp1            v4.4s,  v20.4s, v21.4s  // z[-i-2, -i-1, +i, i+1].re
        uzp2            v6.4s,  v20.4s, v21.4s  // z[-i-2, -i-1, +i, i+1].im

        st1             {v5.4s},  [x10], x8
        st1             {v7.4s},  [x11], x7

        uzp1            v5.4s,  v2.4s,  v3.4s   // twidlle_exptab[-i-2, -i-1, +i, i+1].re
        uzp2            v7.4s,  v2.4s,  v3.4s   // twidlle_exptab[-i-2, -i-1, +i, i+1].im

        fmul            v1.4s,  v6.4s,  v5.4s
        fmul            v0.4s,  v6.4s,  v7.4s
        b.gt            2b

        fmla            v1.4s,  v4.4s,  v7.4s
        fmls            v0.4s,  v4.4s,  v5.4s
        ext             v1.16b, v1.16b, v1.16b, #8
        fmul            v0.4s,  v0.4s,  v31.s[0]
        rev64           v1.4s,  v1.4s
        fmul            v1.4s,  v1.4s,  v31.s[0]
        zip1            v5.4s,  v0.4s,  v1.4s
        zip2            v7.4s,  v0.4s,  v1.4s
        st1             {v5.4s},  [x10], x8
        st1             {v7.4s},  [x11], x7

        ldp             x21, x30, [sp]
        add             sp,  sp,  #0x20
        ret
endfunc

// [0] = exp(2 * i * pi / 5), [1] = exp(2 * i * pi * 2 / 5)
const   fact5,          align=4
        .float           0.30901699437494745, 0.95105651629515353
        .float          -0.80901699437494734, 0.58778525229247325
endconst

/*
 * Copyright (c) Lynne
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

/* Open `doc/transforms.md` to see the code upon which the transforms here were
 * based upon.
 *
 * File conventions:
 * GPRs:    x0-x3   - arguments, untouched
 *          x4      - Lookup table base pointer
 *          x5-x6   - macro ld1 temps/function scratch
 *          x7-x9   - FFT table state
 *          x10-x17 - lookup table/macro scratch
 *          w19-w20 - current/target length when needed
 *          x21-x22 - len*2, len*6
 *
 * Vectors: v0-v7   - coefficients
 *          v8-v15  - coefficients when needed, otherwise untouched
 *          v16-v30 - used as needed
 *          v31     - -1.0, +1.0, -1.0, +1.0. Never touched after loading.
 *
 * Stack:   backup for v8-v15 and x19-x22 when needed, and transform lengths
 */

#define M_SQRT1_2 0.707106781186547524401
#define COS16_1   0.92387950420379638671875
#define COS16_3   0.3826834261417388916015625

/* We only ever load this once at the start, and then live with losing an
 * entire register as we need to lug this all the time everywhere.
 * Clearly should be integrated into an fsadd and fmlsa, but "muh RISC!". */
const subadd, align=4
        .float -1.0,  1.0, -1.0,  1.0
endconst

.macro LOAD_SUBADD
        movrel          x5, subadd
        ld1             { v31.4s }, [x5]
.endm

.macro SETUP_LUT no_lut=0
.if \no_lut == 0
        ldr             x4, [x0, #8]
.endif
.endm

.macro LOAD_INPUT dst1, dst2, dst3, dst4, src, no_lut=0, discont=0
.if \no_lut == 1
.if \discont == 1
        ldp             q\dst1\(), q\dst2\(), [\src\()]
        ldp             q\dst3\(), q\dst4\(), [\src\(), #32]
        add             \src\(), \src\(), #64
.else
        ld1             { v\dst1\().4s, v\dst2\().4s, v\dst3\().4s, v\dst4\().4s }, [\src], #64
.endif
.else
        ldp             w10, w11, [x4, #0 ]
        ldp             w12, w13, [x4, #8 ]
        ldp             w14, w15, [x4, #16]
        ldp             w16, w17, [x4, #24]

        add             x4, x4, #32

        ldr             d\dst1, [\src, x10, lsl #3]
        add             x11, \src, x11, lsl #3
        ldr             d\dst2, [\src, x12, lsl #3]
        add             x13, \src, x13, lsl #3
        ldr             d\dst3, [\src, x14, lsl #3]
        add             x15, \src, x15, lsl #3
        ldr             d\dst4, [\src, x16, lsl #3]
        add             x17, \src, x17, lsl #3

        ld1             { v\dst1\().d }[1], [x11]
        ld1             { v\dst2\().d }[1], [x13]
        ld1             { v\dst3\().d }[1], [x15]
        ld1             { v\dst4\().d }[1], [x17]
.endif
.endm

.macro FFT4 e0, o0, standalone
        fadd            v16.4s, \e0\().4s, \o0\().4s         // r1..4
        fsub            \e0\().4s, \e0\().4s, \o0\().4s      // t1..4

        rev64           v18.4s, \e0\().4s

        zip2            \o0\().2d, v16.2d, \e0\().2d
        zip1            v17.2d, v16.2d, \e0\().2d

        mov             \o0\().d[1], v18.d[1]

        fadd            \e0\().4s, v17.4s, \o0\().4s         // a1,2 b1,4
        fsub            v16.4s,    v17.4s, \o0\().4s         // a3,4 b3,2

        mov             \o0\().16b, v16.16b                  // Swap once again...
        mov             \o0\().s[3], \e0\().s[3]
        mov             \e0\().s[3], v16.s[3]

.if \standalone == 0
        uzp2            \o0\().2d, \e0\().2d, \o0\().2d
        uzp1            \e0\().2d, \e0\().2d, v16.2d
.endif
.endm

const shuf_4pt_x2, align=4
        .byte   24, 25, 26, 27 // reg2, 3
        .byte   12, 13, 14, 15 // reg1, 4
        .byte    8,  9, 10, 11 // reg1, 3
        .byte   28, 29, 30, 31 // reg2, 4
endconst

// Identical to FFT4, but does 2 transforms in parallel, with no deinterleaving
.macro FFT4_X2 e0, o0, e1, o1, \
               t0=v16, t1=v17, t2=v18, t3=v19, t4=v20, t5=v21, t6=v22

        fadd            \t0\().4s, \e0\().4s, \o0\().4s                     // r1234
        fadd            \t2\().4s, \e1\().4s, \o1\().4s                     // r1234
        fsub            \e0\().4s, \e0\().4s, \o0\().4s                     // t1234
        fsub            \e1\().4s, \e1\().4s, \o1\().4s                     // t1234

        movrel          x5, shuf_4pt_x2

        rev64           \t4\().4s, \e0\().4s
        rev64           \t5\().4s, \e1\().4s

        zip2            \o0\().2d, \t0\().2d, \e0\().2d                     // t3,4 r3,4
        zip2            \o1\().2d, \t2\().2d, \e1\().2d                     // t3,4 r3,4

        ld1             { \t6\().16b }, [x5]

        mov             \o0\().d[1], \t4\().d[1]
        mov             \o1\().d[1], \t5\().d[1]

        zip1            \t1\().2d, \t0\().2d, \e0\().2d                     // t1,2 r1,2
        zip1            \t3\().2d, \t2\().2d, \e1\().2d                     // t1,2 r1,2

        fsub            \t4\().4s, \t1\().4s, \o0\().4s                     // a34 b32
        fadd            \t5\().4s, \t1\().4s, \o0\().4s                     // a12 b14
        fsub            \t2\().4s, \t3\().4s, \o1\().4s                     // a34 b32
        fadd            \t3\().4s, \t3\().4s, \o1\().4s                     // a12 b14

        // TODO: experiment with movs instead of tables here
        tbl             \o0\().16b, { \t4\().16b, \t5\().16b }, \t6\().16b  // b1234
        tbl             \o1\().16b, { \t2\().16b, \t3\().16b }, \t6\().16b  // b1234

        zip1            \e0\().2d, \t5\().2d, \t4\().2d                     // a1234
//        zip2            \o0\().2d, \t5\().2d, \t4\().2d                     // b1432
        zip1            \e1\().2d, \t3\().2d, \t2\().2d                     // a1234
//        zip2            \o1\().2d, \t3\().2d, \t2\().2d                     // b1432
//        rev64           \o0\().4s, \o0\().4s                                // b4123
//        rev64           \o1\().4s, \o1\().4s                                // b4123
//        ext             \o0\().16b, \o0\().16b, \o0\().16b, #4              // b1234
//        ext             \o1\().16b, \o1\().16b, \o1\().16b, #4              // b1234
.endm

const tab_8pt, align=4
        .float M_SQRT1_2, -M_SQRT1_2, -M_SQRT1_2,  M_SQRT1_2
endconst

.macro FFT8 e0, e1, o0, o1, \
            t0=v16, t1=v17, t2=v18, t3=v19, t4=v20, t5=v21, t6=v22

        movrel          x5, tab_8pt

        fsub            \t1\().4s, \e1\().4s, \o1\().4s             // j1234
        fadd            \o1\().4s, \e1\().4s, \o1\().4s             // k1234
        fsub            \t0\().4s, \e0\().4s, \o0\().4s             // r1234
        fadd            \o0\().4s, \e0\().4s, \o0\().4s             // q1234

        ld1             { \t5\().4s }, [x5]

        ext             \t4\().16b, \o1\().16b, \o1\().16b, #12
        rev64           \t4\().4s,  \t4\().4s

        ext             \t2\().16b, \o0\().16b, \t4\().16b, #8      // o0[0,1], o1[3,2]
        mov             \o0\().d[1], \t4\().d[1]                    // o0[3, 4]; o1[1, 4]

        fsub            \e1\().4s, \o0\().4s, \t2\().4s             // s34, g43
        fadd            \t2\().4s, \o0\().4s, \t2\().4s             // s12, g12

        rev64           \t6\().4s, v31.4s                           // 1, -1, 1, -1
        dup             \o0\().2d, \t0\().d[0]                      // r1212
        dup             \o1\().2d, \t0\().d[1]                      // r3434

        rev64           \t4\().4s, \e1\().4s                        // xxg34
        rev64           \o1\().4s, \o1\().4s                        // r4343

        ext             \t6\().16b, v31.16b, \t6\().16b, #8         // -1, 1, 1, -1
        zip1            \t3\().2d, \t2\().2d, \e1\().2d             // s1234
        zip2            \t2\().2d, \t2\().2d, \t4\().2d             // g1234

        fadd            \e0\().4s, \t3\().4s, \t2\().4s             // out_e1
        fsub            \e1\().4s, \t3\().4s, \t2\().4s             // out_e2

        fmul            \t1\().4s, \t1\().4s, \t5\().4s             // j * +--+M_SQRT1_2
        fmls            \o0\().4s, \o1\().4s, \t6\().4s             // z1234

        rev64           \t4\().4s, \t1\().4s                        // j2143
        fmla            \t1\().4s, \t4\().4s, v31.4s                // l2143

        rev64           \t4\().4s, \t1\().4s                        // l1234
        ext             \t4\().16b, \t4\().16b, \t4\().16b, #8      // l3412

        fmla            \t4\().4s, \t1\().4s, v31.4s                // t1234

        fadd            \o1\().4s, \o0\().4s, \t4\().4s             // out_o2
        fsub            \o0\().4s, \o0\().4s, \t4\().4s             // out_o1
.endm

// Identical as FFT8, but does 2 transforms in parallel
.macro FFT8_X2 e0, e1, o0, o1, e2, e3, o2, o3

        movrel          x5, tab_8pt

        fadd            v19.4s, \e3\().4s, \o3\().4s             // k1234
        fadd            v17.4s, \e1\().4s, \o1\().4s             // k1234
        fadd            v18.4s, \e2\().4s, \o2\().4s             // q1234
        fadd            v16.4s, \e0\().4s, \o0\().4s             // q1234

        ld1             { v23.4s }, [x5]

        ext             v22.16b, v19.16b, v19.16b, #12
        ext             v21.16b, v17.16b, v17.16b, #12

        rev64           v22.4s,  v22.4s
        rev64           v21.4s,  v21.4s

        ext             v19.16b, v18.16b, v22.16b, #8
        ext             v17.16b, v16.16b, v21.16b, #8

        mov             v18.d[1], v22.d[1]
        mov             v21.d[0], v16.d[0]

        fadd            v22.4s, v18.4s, v19.4s                   // s12, g12
        fsub            v19.4s, v18.4s, v19.4s                   // s34, g43
        fsub            v18.4s, v21.4s, v17.4s                   // s34, g43
        fadd            v16.4s, v21.4s, v17.4s                   // s12, g12

        fsub            \e0\().4s, \e0\().4s, \o0\().4s          // r1234
        fsub            v20.4s, \e1\().4s, \o1\().4s             // j1234
        fsub            \e2\().4s, \e2\().4s, \o2\().4s          // r1234
        fsub            v21.4s, \e3\().4s, \o3\().4s             // j1234

        rev64           v24.4s, v31.4s                           // 1, -1, 1, -1
        zip1            v17.2d, v16.2d, v18.2d                   // s1234
        zip1            \e1\().2d, v22.2d, v19.2d                // s1234

        rev64           v18.4s, v18.4s                           // xxg34
        rev64           v19.4s, v19.4s                           // xxg34

        zip2            v16.2d, v16.2d, v18.2d                   // g1234
        zip2            \e3\().2d, v22.2d, v19.2d                // g1234

        dup             \o0\().2d, \e0\().d[0]                   // r1212
        dup             \o1\().2d, \e0\().d[1]                   // r3434
        dup             \o2\().2d, \e2\().d[0]                   // r1212
        dup             \o3\().2d, \e2\().d[1]                   // r3434

        fadd            \e2\().4s, \e1\().4s, \e3\().4s          // out_e1
        fsub            \e3\().4s, \e1\().4s, \e3\().4s          // out_e2
        fadd            \e0\().4s, v17.4s, v16.4s                // out_e1
        fsub            \e1\().4s, v17.4s, v16.4s                // out_e2

        ext             v24.16b, v31.16b, v24.16b, #8            // -1, 1, 1, -1
        rev64           \o1\().4s, \o1\().4s                     // r4343
        rev64           \o3\().4s, \o3\().4s                     // r4343

        fmul            v19.4s, v20.4s, v23.4s                   // j * +--+M_SQRT1_2
        fmul            v21.4s, v21.4s, v23.4s                   // j * +--+M_SQRT1_2

        rev64           v20.4s, v19.4s                           // j2143
        rev64           v18.4s, v21.4s                           // j2143

        fmls            \o0\().4s, \o1\().4s, v24.4s             // z1234
        fmls            \o2\().4s, \o3\().4s, v24.4s             // z1234

        fmla            v19.4s, v20.4s, v31.4s                   // l2143
        fmla            v21.4s, v18.4s, v31.4s                   // l2143

        rev64           v20.4s, v19.4s                           // l1234
        rev64           v18.4s, v21.4s                           // l1234
        ext             v20.16b, v20.16b, v20.16b, #8            // l3412
        ext             v18.16b, v18.16b, v18.16b, #8            // l3412

        fmla            v20.4s, v19.4s, v31.4s                   // t1234
        fmla            v18.4s, v21.4s, v31.4s                   // t1234

        fadd            \o1\().4s, \o0\().4s, v20.4s             // out_o2
        fadd            \o3\().4s, \o2\().4s, v18.4s             // out_o2
        fsub            \o0\().4s, \o0\().4s, v20.4s             // out_o1
        fsub            \o2\().4s, \o2\().4s, v18.4s             // out_o1
.endm

const tab_16pt, align=4
        .float   -COS16_1,    COS16_1,   -COS16_3,    COS16_3    // Could be +-+- too
        .float    COS16_3,    COS16_3,    COS16_1,    COS16_1
        .float        1.0,        1.0,  M_SQRT1_2,  M_SQRT1_2
endconst

// 16-point FFT
// t3, t4, t5, t6 must be sequential
.macro FFT16 e0, e1, e2, e3, o0, o1, o2, o3, \
             t0=v16, t1=v17, t2=v18, t3=v19, t4=v20, t5=v21, t6=v22

        FFT8            \e0, \e1, \e2, \e3, \t0, \t1, \t2, \t3, \t4, \t5, \t6
        FFT4_X2         \o0, \o1, \o2, \o3, \t0, \t1, \t2, \t3, \t4, \t5, \t6

        movrel          x5, tab_16pt

        rev64           \t0\().4s, \o0\().4s                     // z[ 8, 9].imre
        rev64           \t1\().4s, \o2\().4s                     // z[10,11].imre

        ins             \t0\().d[0], xzr
        ins             \t1\().d[0], xzr

        ld1             { \t4\().4s, \t5\().4s, \t6\().4s }, [x5]
        // TODO: We could derive \t4\() or \t5\() from either, but it seems cheaper to load

        fmla            \o2\().4s, \t1\().4s, v31.4s             // s[4567]
        fmls            \o0\().4s, \t0\().4s, v31.4s             // s[0123]

        fmul            \t2\().4s, \o1\().4s, \t4\().4s
        fmul            \t3\().4s, \o3\().4s, \t4\().4s

        rev64           \o3\().4s, \o3\().4s
        rev64           \o1\().4s, \o1\().4s

        fmla            \t3\().4s, \o3\().4s, \t5\().4s          // s[12, 13, 14, 15]
        fmls            \t2\().4s, \o1\().4s, \t5\().4s          // s[ 8,  9, 10, 11]

        fmul            \t1\().4s, \o2\().4s, \t6\().4s          // s[4567] * mult
        fmul            \t0\().4s, \o0\().4s, \t6\().4s          // s[0123] * mult

        mov             \o1\().16b, \t3\().16b
        mov             \o2\().16b, \t1\().16b

        fsub            \t3\().4s, \t3\().4s, \t2\().4s          // y34, u34
        fsub            \t1\().4s, \t1\().4s, \t0\().4s          // w34, x34

        fadd            \t2\().4s, \t2\().4s, \o1\().4s          // y56, u56
        rev64           \t3\().4s, \t3\().4s
        fadd            \t0\().4s, \t0\().4s, \o2\().4s          // w56, x56
        rev64           \t1\().4s, \t1\().4s

        fmul            \t2\().4s, \t2\().4s, v31.4s
        fmul            \t1\().4s, \t1\().4s, v31.4s

        fadd            \o3\().4s, \e3\().4s, \t3\().4s
        fsub            \o2\().4s, \e3\().4s, \t3\().4s
        fsub            \o1\().4s, \e2\().4s, \t2\().4s
        fadd            \o0\().4s, \e2\().4s, \t2\().4s

        fsub            \e2\().4s, \e0\().4s, \t0\().4s
        fadd            \e0\().4s, \e0\().4s, \t0\().4s
        fsub            \e3\().4s, \e1\().4s, \t1\().4s
        fadd            \e1\().4s, \e1\().4s, \t1\().4s
.endm

function ff_tx_fft2_float_neon, export=1
        ld2r            { v0.2d, v1.2d }, [x2]

        fneg            v2.2s, v1.2s
        mov             v2.d[1], v1.d[0]

        fsub            v2.4s, v0.4s, v2.4s

        st1             { v2.4s }, [x1]
        ret
endfunc

.macro FFT4_FN name, inv
function ff_tx_fft4_\name\()_float_neon, export=1
        ld1             {v0.4s, v1.4s}, [x2]

.if \inv == 1
        mov             v2.d[0], v0.d[1]
        mov             v0.d[1], v1.d[1]
        mov             v1.d[1], v2.d[0]
.endif

        FFT4            v0, v1, 1

        st1             { v0.4s, v1.4s }, [x1]
        ret
endfunc
.endm

FFT4_FN fwd, 0
FFT4_FN inv, 1

.macro FFT8_FN name, no_perm
function ff_tx_fft8_\name\()_neon, export=1
        SETUP_LUT       \no_perm
        LOAD_INPUT      0, 1, 2, 3, x2, \no_perm

        LOAD_SUBADD
        FFT8            v0, v1, v2, v3

        zip1            v16.2d, v0.2d, v2.2d
        zip2            v17.2d, v0.2d, v2.2d
        zip1            v18.2d, v1.2d, v3.2d
        zip2            v19.2d, v1.2d, v3.2d
        st1             { v16.4s, v17.4s, v18.4s, v19.4s }, [x1]

        ret
endfunc
.endm

FFT8_FN float,    0
FFT8_FN ns_float, 1

.macro FFT16_FN name, no_perm
function ff_tx_fft16_\name\()_neon, export=1
        SETUP_LUT       \no_perm
        LOAD_INPUT      0, 1, 2, 3, x2, \no_perm
        LOAD_INPUT      4, 5, 6, 7, x2, \no_perm

        LOAD_SUBADD
        FFT16           v0, v1, v2, v3, v4, v5, v6, v7

        zip1            v20.2d, v0.2d, v4.2d
        zip2            v21.2d, v0.2d, v4.2d
        zip1            v22.2d, v1.2d, v6.2d
        zip2            v23.2d, v1.2d, v6.2d
        st1             { v20.4s, v21.4s, v22.4s, v23.4s }, [x1], #64

        zip1            v24.2d, v2.2d, v5.2d
        zip2            v25.2d, v2.2d, v5.2d
        zip1            v26.2d, v3.2d, v7.2d
        zip2            v27.2d, v3.2d, v7.2d
        st1             { v24.4s, v25.4s, v26.4s, v27.4s }, [x1]

        ret
endfunc
.endm

FFT16_FN float,    0
FFT16_FN ns_float, 1

.macro SETUP_SR_RECOMB len, re, im, dec
        ldr             w5, =(\len - 4*7)
        movrel          \re, X(ff_tx_tab_\len\()_float)
        add             \im, \re, x5
        mov             \dec, #-32

.if \len > 32
        mov             x21, #2*\len
        add             x22, x21, x21, lsl #1
.endif
.endm

.macro SR_COMBINE e0, e1, e2, e3, e4, e5, e6, e7, \
                  o0, o1, o2, o3, o4, o5, o6, o7, \
                  re, im, dec, swap_im, \
                  t0=v16, t1=v17, t2=v18, t3=v19, t4=v20, t5=v21, \
                  t6=v22, t7=v23, t8=v24, t9=v25, ta=v26, tb=v27

        ld1             { \t8\().4s, \t9\().4s }, [\im], \dec
        ld1             { \t0\().4s, \t1\().4s }, [\re], #32

.if \swap_im == 1
        ext             \t2\().16b, \t9\().16b, \t9\().16b, #8
        ext             \t3\().16b, \t8\().16b, \t8\().16b, #8
.else
        ext             \t2\().16b, \t8\().16b, \t8\().16b, #8
        ext             \t3\().16b, \t9\().16b, \t9\().16b, #8
.endif

        trn1            \t4\().4s, \t0\().4s, \t0\().4s      // cos0022
        trn2            \t0\().4s, \t0\().4s, \t0\().4s      // cos4466
        trn1            \t5\().4s, \t1\().4s, \t1\().4s      // cos1133
        trn2            \t1\().4s, \t1\().4s, \t1\().4s      // cos5577

        rev64           \t6\().4s, \o0\().4s                 // E m2[0,1].imre
        rev64           \t7\().4s, \o2\().4s                 // O m2[0,1].imre
        rev64           \t8\().4s, \o4\().4s                 // E m2[2,3].imre
        rev64           \t9\().4s, \o6\().4s                 // O m2[2,3].imre

        fmul            \t6\().4s, \t6\().4s, \t4\().4s      // E m2[0,1].imre*t1[0,2]
        fmul            \t7\().4s, \t7\().4s, \t0\().4s      // O m2[0,1].imre*t1[0,2]
        fmul            \t8\().4s, \t8\().4s, \t4\().4s      // E m2[2,3].imre*t1[0,2]
        fmul            \t9\().4s, \t9\().4s, \t0\().4s      // O m2[2,3].imre*t1[0,2]

        rev64           \ta\().4s, \o1\().4s                 // E m3[0,1].imre
        rev64           \tb\().4s, \o3\().4s                 // O m3[0,1].imre
        rev64           \t4\().4s, \o5\().4s                 // E m3[2,3].imre
        rev64           \t0\().4s, \o7\().4s                 // O m3[2,3].imre

        fmul            \ta\().4s, \ta\().4s, \t5\().4s      // E m3[0,1].imre*t1[4,6]
        fmul            \tb\().4s, \tb\().4s, \t1\().4s      // O m3[0,1].imre*t1[4,6]
        fmul            \t4\().4s, \t4\().4s, \t5\().4s      // E m3[2,3].imre*t1[4,6]
        fmul            \t0\().4s, \t0\().4s, \t1\().4s      // O m3[2,3].imre*t1[4,6]

        trn1            \t5\().4s, \t3\().4s, \t3\().4s      // wim2200
        trn2            \t3\().4s, \t3\().4s, \t3\().4s      // wim3311
        trn1            \t1\().4s, \t2\().4s, \t2\().4s      // wim6644
        trn2            \t2\().4s, \t2\().4s, \t2\().4s      // wim7755

        fmul            \t5\().4s, \t5\().4s, v31.4s
        fmul            \t3\().4s, \t3\().4s, v31.4s
        fmul            \t1\().4s, \t1\().4s, v31.4s
        fmul            \t2\().4s, \t2\().4s, v31.4s

        fmla            \t7\().4s, \o2\().4s, \t5\().4s      // O w0123
        fmls            \t9\().4s, \o6\().4s, \t5\().4s      // O j0123
        fmla            \t6\().4s, \o0\().4s, \t3\().4s      // E w0123
        fmls            \t8\().4s, \o4\().4s, \t3\().4s      // E j0123

        fmla            \ta\().4s, \o1\().4s, \t2\().4s      // E w4567
        fmla            \tb\().4s, \o3\().4s, \t1\().4s      // O w4567
        fmls            \t4\().4s, \o5\().4s, \t2\().4s      // E j4567
        fmls            \t0\().4s, \o7\().4s, \t1\().4s      // O j4567

        fsub            \t2\().4s, \t7\().4s, \t9\().4s
        fsub            \t1\().4s, \t8\().4s, \t6\().4s
        fsub            \t3\().4s, \t4\().4s, \ta\().4s
        fsub            \t5\().4s, \t0\().4s, \tb\().4s

        fadd            \t6\().4s, \t8\().4s, \t6\().4s
        fadd            \t7\().4s, \t9\().4s, \t7\().4s
        fadd            \t8\().4s, \t4\().4s, \ta\().4s
        fadd            \t9\().4s, \t0\().4s, \tb\().4s

        fmul            \t1\().4s, \t1\().4s, v31.4s
        fmul            \t2\().4s, \t2\().4s, v31.4s
        fmul            \t3\().4s, \t3\().4s, v31.4s
        fmul            \t5\().4s, \t5\().4s, v31.4s

        rev64           \t6\().4s, \t6\().4s
        rev64           \t8\().4s, \t8\().4s
        rev64           \t7\().4s, \t7\().4s
        rev64           \t9\().4s, \t9\().4s

        fsub            \o0\().4s, \e0\().4s, \t6\().4s
        fsub            \o1\().4s, \e1\().4s, \t8\().4s
        fsub            \o2\().4s, \e2\().4s, \t1\().4s
        fsub            \o3\().4s, \e3\().4s, \t3\().4s

        fsub            \o4\().4s, \e4\().4s, \t7\().4s
        fsub            \o5\().4s, \e6\().4s, \t9\().4s
        fadd            \o6\().4s, \e5\().4s, \t2\().4s
        fsub            \o7\().4s, \e7\().4s, \t5\().4s

        fadd            \e0\().4s, \e0\().4s, \t6\().4s
        fadd            \e1\().4s, \e1\().4s, \t8\().4s
        fadd            \e2\().4s, \e2\().4s, \t1\().4s
        fadd            \e3\().4s, \e3\().4s, \t3\().4s

        fadd            \e4\().4s, \e4\().4s, \t7\().4s
        fsub            \e5\().4s, \e5\().4s, \t2\().4s      // swapped
        fadd            \e6\().4s, \e6\().4s, \t9\().4s      // swapped
        fadd            \e7\().4s, \e7\().4s, \t5\().4s
.endm

.macro SR_COMBINE_HALF e0, e1, e2, e3, \
                       o0, o1, o2, o3, \
                       c0, c1, c2, c3, \
                       t0, t1, t2, t3, t4, t5, part

.if \part == 0
        trn1            \t4\().4s, \c0\().4s, \c0\().4s   // cos0022
        trn1            \c1\().4s, \c1\().4s, \c1\().4s   // cos1133
.else
        trn2            \t4\().4s, \c0\().4s, \c0\().4s   // cos0022
        trn2            \c1\().4s, \c1\().4s, \c1\().4s   // cos1133
.endif
.if \part == 0
        trn2            \t5\().4s, \c2\().4s, \c2\().4s   // wim7755
        trn2            \c3\().4s, \c3\().4s, \c3\().4s   // wim3311
.else
        trn1            \t5\().4s, \c2\().4s, \c2\().4s   // wim7755
        trn1            \c3\().4s, \c3\().4s, \c3\().4s   // wim3311
.endif

        fmul            \t5\().4s, \t5\().4s, v31.4s
        fmul            \c3\().4s, \c3\().4s, v31.4s

        rev64           \t0\().4s, \o0\().4s              // E m2[0,1].imre
        rev64           \t1\().4s, \o2\().4s              // E m2[2,3].imre
        rev64           \t2\().4s, \o1\().4s              // E m3[0,1].imre
        rev64           \t3\().4s, \o3\().4s              // E m3[2,3].imre

        fmul            \o0\().4s, \o0\().4s, \c3\().4s   // E m2[0,1].imre*t1[0,2]
        fmul            \o1\().4s, \o1\().4s, \t5\().4s   // E m3[0,1].imre*t1[4,6]
        fmla            \o0\().4s, \t0\().4s, \t4\().4s   // E w0123
        fmla            \o1\().4s, \t2\().4s, \c1\().4s   // E w4567

        fmul            \t1\().4s, \t1\().4s, \t4\().4s   // E m2[2,3].imre*t1[0,2]
        fmul            \t3\().4s, \t3\().4s, \c1\().4s   // E m3[2,3].imre*t1[4,6]
        fmls            \t1\().4s, \o2\().4s, \c3\().4s   // E j0123
        fmls            \t3\().4s, \o3\().4s, \t5\().4s   // E j4567

        fsub            \t0\().4s, \t1\().4s, \o0\().4s
        fadd            \t1\().4s, \t1\().4s, \o0\().4s
        fadd            \t2\().4s, \t3\().4s, \o1\().4s
        fsub            \t3\().4s, \t3\().4s, \o1\().4s

        fmul            \t0\().4s, \t0\().4s, v31.4s
        fmul            \t3\().4s, \t3\().4s, v31.4s

        rev64           \t1\().4s, \t1\().4s
        rev64           \t2\().4s, \t2\().4s

.if \part == 0
        fsub            \o0\().4s, \e0\().4s, \t1\().4s
        fsub            \o1\().4s, \e1\().4s, \t2\().4s
        fsub            \o2\().4s, \e2\().4s, \t0\().4s
        fsub            \o3\().4s, \e3\().4s, \t3\().4s
.else
        fsub            \o0\().4s, \e0\().4s, \t1\().4s
        fadd            \o2\().4s, \e1\().4s, \t2\().4s
        fsub            \o1\().4s, \e2\().4s, \t0\().4s
        fadd            \o3\().4s, \e3\().4s, \t3\().4s
.endif

.if \part == 0
        fadd            \e0\().4s, \e0\().4s, \t1\().4s
        fadd            \e1\().4s, \e1\().4s, \t2\().4s
        fadd            \e2\().4s, \e2\().4s, \t0\().4s
        fadd            \e3\().4s, \e3\().4s, \t3\().4s
.else
        fadd            \e0\().4s, \e0\().4s, \t1\().4s
        fsub            \e1\().4s, \e1\().4s, \t2\().4s   // swapped
        fadd            \e2\().4s, \e2\().4s, \t0\().4s   // swapped
        fsub            \e3\().4s, \e3\().4s, \t3\().4s
.endif
.endm

/* Same as SR_COMBINE_HALF, but heroically tries to use 3 temporary registers
 * without touching the tables. */
.macro SR_COMBINE_LITE e0, e1, e2, e3, \
                       o0, o1, o2, o3, \
                       c0, c1, c2, c3, \
                       t0, t1, t2, part

        rev64           \t0\().4s, \o0\().4s              // E m2[0,1].imre
        rev64           \t1\().4s, \o2\().4s              // E m2[2,3].imre
.if \part == 0
        trn2            \t2\().4s, \c3\().4s, \c3\().4s   // wim3311
.else
        trn1            \t2\().4s, \c3\().4s, \c3\().4s   // wim3311
.endif
        fmul            \t2\().4s, \t2\().4s, v31.4s
        fmul            \o2\().4s, \o2\().4s, \t2\().4s
        fmul            \o0\().4s, \o0\().4s, \t2\().4s   // E m2[0,1].imre*t1[0,2]
.if \part == 0
        trn1            \t2\().4s, \c0\().4s, \c0\().4s   // cos0022
.else
        trn2            \t2\().4s, \c0\().4s, \c0\().4s   // cos0022
.endif
        fmul            \t1\().4s, \t1\().4s, \t2\().4s   // E m2[2,3].imre*t1[0,2]
        fmla            \o0\().4s, \t0\().4s, \t2\().4s   // E w0123
        fsub            \t1\().4s, \t1\().4s, \o2\().4s   // E j0123

        rev64           \t2\().4s, \o1\().4s              // E m3[0,1].imre
        rev64           \o2\().4s, \o3\().4s              // E m3[2,3].imre

.if \part == 0
        trn2            \t0\().4s, \c2\().4s, \c2\().4s   // wim7755
.else
        trn1            \t0\().4s, \c2\().4s, \c2\().4s   // wim7755
.endif
        fmul            \t0\().4s, \t0\().4s, v31.4s

        fmul            \o1\().4s, \o1\().4s, \t0\().4s   // E m3[0,1].imre*t1[4,6]
        fmul            \o3\().4s, \o3\().4s, \t0\().4s

.if \part == 0
        trn1            \t0\().4s, \c1\().4s, \c1\().4s   // cos1133
.else
        trn2            \t0\().4s, \c1\().4s, \c1\().4s   // cos1133
.endif
        fmul            \o2\().4s, \o2\().4s, \t0\().4s   // E m3[2,3].imre*t1[4,6]
        fmla            \o1\().4s, \t2\().4s, \t0\().4s   // E w4567
        fsub            \o2\().4s, \o2\().4s, \o3\().4s   // E j4567

        fsub            \t0\().4s, \t1\().4s, \o0\().4s
        fadd            \o0\().4s, \t1\().4s, \o0\().4s
        fadd            \t2\().4s, \o2\().4s, \o1\().4s
        fsub            \t1\().4s, \o2\().4s, \o1\().4s

        fmul            \t0\().4s, \t0\().4s, v31.4s
        fmul            \t1\().4s, \t1\().4s, v31.4s

        rev64           \t2\().4s, \t2\().4s
        rev64           \o0\().4s, \o0\().4s

.if \part == 0
        fsub            \o1\().4s, \e1\().4s, \t2\().4s
        fsub            \o2\().4s, \e2\().4s, \t0\().4s
        fsub            \o3\().4s, \e3\().4s, \t1\().4s
.else
        fadd            \o2\().4s, \e1\().4s, \t0\().4s
        fsub            \o1\().4s, \e2\().4s, \t2\().4s
        fadd            \o3\().4s, \e3\().4s, \t1\().4s
.endif

.if \part == 0
        fadd            \e1\().4s, \e1\().4s, \t2\().4s
        fadd            \e2\().4s, \e2\().4s, \t0\().4s
        fadd            \e3\().4s, \e3\().4s, \t1\().4s
.else
        fsub            \e1\().4s, \e1\().4s, \t0\().4s   // swapped
        fadd            \e2\().4s, \e2\().4s, \t2\().4s   // swapped
        fsub            \e3\().4s, \e3\().4s, \t1\().4s
.endif

        mov             \t1\().16b, \o0\().16b

        fsub            \o0\().4s, \e0\().4s, \t1\().4s
        fadd            \e0\().4s, \e0\().4s, \t1\().4s
.endm

.macro SR_COMBINE_4 len, part, off
        add             x10, x1, x21
        add             x11, x1, x21, lsl #1
        add             x12, x1, x22

        ldp             q0,  q1,  [x1,  #((0 + \part)*32 + \off)]
        ldp             q4,  q5,  [x1,  #((2 + \part)*32 + \off)]
        ldp             q2,  q3,  [x10, #((0 + \part)*32 + \off)]
        ldp             q6,  q7,  [x10, #((2 + \part)*32 + \off)]

        ldp             q8,  q9,  [x11, #((0 + \part)*32 + \off)]
        ldp             q10, q11, [x11, #((2 + \part)*32 + \off)]
        ldp             q12, q13, [x12, #((0 + \part)*32 + \off)]
        ldp             q14, q15, [x12, #((2 + \part)*32 + \off)]

        SR_COMBINE      v0,  v1,  v2,  v3,  v4,  v6,  v5,  v7, \
                        v8,  v9, v10, v11, v12, v13, v14, v15, \
                        x7,  x8, x9, 0

        stp             q0,  q1,  [x1,  #((0 + \part)*32 + \off)]
        stp             q4,  q5,  [x1,  #((2 + \part)*32 + \off)]
        stp             q2,  q3,  [x10, #((0 + \part)*32 + \off)]
        stp             q6,  q7,  [x10, #((2 + \part)*32 + \off)]

        stp             q8,  q9,  [x11, #((0 + \part)*32 + \off)]
        stp             q12, q13, [x11, #((2 + \part)*32 + \off)]
        stp             q10, q11, [x12, #((0 + \part)*32 + \off)]
        stp             q14, q15, [x12, #((2 + \part)*32 + \off)]
.endm

.macro SR_COMBINE_FULL len, off=0
        add             x10, x1, x21
        add             x11, x1, x21, lsl #1
        add             x12, x1, x22

        SR_COMBINE_4    \len, 0, \off
        SR_COMBINE_4    \len, 1, \off
        SR_COMBINE_4    \len, 4, \off
        SR_COMBINE_4    \len, 5, \off
.endm

.macro SR_COMBINE_D2 part, off
        add             x10,  x1, #((\part)*32 + \off)
        add             x11, x14, #((\part)*32 + \off)
        add             x12, x15, #((\part)*32 + \off)
        add             x13, x16, #((\part)*32 + \off)

        ldp             q0,  q1,  [x10]
        ldp             q4,  q5,  [x10, #(2*32)]
        ldp             q2,  q3,  [x11]
        ldp             q6,  q7,  [x11, #(2*32)]

        ldp             q8,  q9,  [x12]
        ldp             q10, q11, [x12, #(2*32)]
        ldp             q12, q13, [x13]
        ldp             q14, q15, [x13, #(2*32)]

        SR_COMBINE      v0,  v1,  v2,  v3,  v4,  v6,  v5,  v7, \
                        v8,  v9, v10, v11, v12, v13, v14, v15, \
                        x7,  x8, x9, 0, \
                        v16, v17, v18, v19, v20, v21, v22, v23, v24, v25, v26, v27

        zip1            v16.2d, v0.2d, v4.2d
        zip2            v17.2d, v0.2d, v4.2d
        zip1            v18.2d, v1.2d, v5.2d
        zip2            v19.2d, v1.2d, v5.2d

        zip1            v20.2d, v2.2d, v6.2d
        zip2            v21.2d, v2.2d, v6.2d
        zip1            v22.2d, v3.2d, v7.2d
        zip2            v23.2d, v3.2d, v7.2d

        ldp             q0,  q1,  [x10, #(1*32)]
        ldp             q4,  q5,  [x10, #(3*32)]
        ldp             q2,  q3,  [x11, #(1*32)]
        ldp             q6,  q7,  [x11, #(3*32)]

        st1             { v16.4s, v17.4s, v18.4s, v19.4s }, [x10], #64
        st1             { v20.4s, v21.4s, v22.4s, v23.4s }, [x11], #64

        zip1            v20.2d, v8.2d, v12.2d
        zip2            v21.2d, v8.2d, v12.2d
        zip1            v22.2d, v9.2d, v13.2d
        zip2            v23.2d, v9.2d, v13.2d
        zip1            v24.2d, v10.2d, v14.2d
        zip2            v25.2d, v10.2d, v14.2d
        zip1            v26.2d, v11.2d, v15.2d
        zip2            v27.2d, v11.2d, v15.2d

        ldp             q8,  q9,  [x12, #(1*32)]
        ldp             q10, q11, [x12, #(3*32)]
        ldp             q12, q13, [x13, #(1*32)]
        ldp             q14, q15, [x13, #(3*32)]

        st1             { v20.4s, v21.4s, v22.4s, v23.4s }, [x12], #64
        st1             { v24.4s, v25.4s, v26.4s, v27.4s }, [x13], #64

        SR_COMBINE      v0,  v1,  v2,  v3,  v4,  v6,  v5,  v7, \
                        v8,  v9, v10, v11, v12, v13, v14, v15, \
                        x7,  x8, x9, 0, \
                        v16, v17, v18, v19, v20, v21, v22, v23, v24, v25, v26, v27

        zip1            v16.2d, v0.2d, v4.2d
        zip2            v17.2d, v0.2d, v4.2d
        zip1            v18.2d, v1.2d, v5.2d
        zip2            v19.2d, v1.2d, v5.2d
        st1             { v16.4s, v17.4s, v18.4s, v19.4s }, [x10]

        zip1            v16.2d, v2.2d, v6.2d
        zip2            v17.2d, v2.2d, v6.2d
        zip1            v18.2d, v3.2d, v7.2d
        zip2            v19.2d, v3.2d, v7.2d
        st1             { v16.4s, v17.4s, v18.4s, v19.4s }, [x11]

        zip1            v20.2d, v8.2d, v12.2d
        zip2            v21.2d, v8.2d, v12.2d
        zip1            v22.2d, v9.2d, v13.2d
        zip2            v23.2d, v9.2d, v13.2d
        st1             { v20.4s, v21.4s, v22.4s, v23.4s }, [x12]

        zip1            v24.2d, v10.2d, v14.2d
        zip2            v25.2d, v10.2d, v14.2d
        zip1            v26.2d, v11.2d, v15.2d
        zip2            v27.2d, v11.2d, v15.2d
        st1             { v24.4s, v25.4s, v26.4s, v27.4s }, [x13]
.endm

.macro SR_COMBINE_DINT off=0
        add             x14, x1, x21
        add             x15, x1, x21, lsl #1
        add             x16, x1, x22

        SR_COMBINE_D2   0, \off
        SR_COMBINE_D2   4, \off
.endm

.macro FFT32_FN name, no_perm
function ff_tx_fft32_\name\()_neon, export=1
        stp             d14, d15, [sp, #-16*4]!
        stp             d8,  d9,  [sp, #16*3]
        stp             d10, d11, [sp, #16*2]
        stp             d12, d13, [sp, #16]

        LOAD_SUBADD
        SETUP_SR_RECOMB 32, x7, x8, x9

        SETUP_LUT       \no_perm
        LOAD_INPUT      0,  1,  2,  3,  x2, \no_perm
        LOAD_INPUT      4,  5,  6,  7,  x2, \no_perm
        LOAD_INPUT      8,  9,  10, 11, x2, \no_perm
        LOAD_INPUT      12, 13, 14, 15, x2, \no_perm

        FFT8_X2         v8, v9, v10, v11, v12, v13, v14, v15
        FFT16           v0, v1, v2, v3, v4, v5, v6, v7

        SR_COMBINE      v0,  v1,  v2,  v3,  v4,  v5,  v6,  v7, \
                        v8,  v9, v10, v11, v12, v13, v14, v15, \
                        x7,  x8,  x9, 0

        zip1            v16.2d, v0.2d, v4.2d
        zip2            v17.2d, v0.2d, v4.2d
        zip1            v18.2d, v1.2d, v6.2d
        zip2            v19.2d, v1.2d, v6.2d
        st1             { v16.4s, v17.4s, v18.4s, v19.4s }, [x1], #64

        zip1            v20.2d, v2.2d, v5.2d
        zip2            v21.2d, v2.2d, v5.2d
        zip1            v22.2d, v3.2d, v7.2d
        zip2            v23.2d, v3.2d, v7.2d
        st1             { v20.4s, v21.4s, v22.4s, v23.4s }, [x1], #64

        zip1            v24.2d, v8.2d, v12.2d
        zip2            v25.2d, v8.2d, v12.2d
        zip1            v26.2d, v9.2d, v13.2d
        zip2            v27.2d, v9.2d, v13.2d
        st1             { v24.4s, v25.4s, v26.4s, v27.4s }, [x1], #64

        zip1            v28.2d, v10.2d, v14.2d
        zip2            v29.2d, v10.2d, v14.2d
        zip1            v30.2d, v11.2d, v15.2d
        zip2            v31.2d, v11.2d, v15.2d
        st1             { v28.4s, v29.4s, v30.4s, v31.4s }, [x1]

        ldp             d12, d13, [sp, #16]
        ldp             d10, d11, [sp, #16*2]
        ldp             d8,  d9,  [sp, #16*3]
        ldp             d14, d15, [sp], #16*4

        ret
endfunc
.endm

FFT32_FN float,    0
FFT32_FN ns_float, 1

.macro cmp_imm reg, imm
.if \imm >= 4096
        cmp             \reg, #((\imm)/4096), lsl #12
.else
        cmp             \reg, #(\imm)
.endif
.endm

.macro SR_TRANSFORM_DEF len, next=0
\len:
        stp             x20, x30, [sp, #-16]!
        mov             w20, #(\len/4)
        mov             x5, #((\len*4) - (\len/1))
        add             x1, x1, x5
        bl              32b
        mov             x5, #((\len*2) - (\len/2))
        add             x1, x1, x5
        bl              32b
        ldp             x20, x30, [sp], #16
        ldr             w5, =(\len*6 + \len/2)
        sub             x1, x1, x5

        SETUP_SR_RECOMB \len, x7, x8, x9

.if \next\() != 0
        cmp_imm         w19, \len
        b.eq            0f

        mov             w5, #(\len/128)
\len\()5:
        SR_COMBINE_FULL \len
        add             x1, x1, 8*32
        subs            w5, w5, 1
        b.gt            \len\()5b

        cmp_imm         w20, \len
        b.gt            \next\()f
        ret
.endif
.endm

.macro FFT_SPLIT_RADIX_FN name, no_perm
function ff_tx_fft_sr_\name\()_neon, export=1
        stp             x21, x22, [sp, #-16*6]!
        stp             d8,  d9,  [sp, #16*5]
        stp             d10, d11, [sp, #16*4]
        stp             d12, d13, [sp, #16*3]
        stp             d14, d15, [sp, #16*2]
        stp             x19, x20, [sp, #16]

        ldr             w19, [x0, #0] // global target
        mov             w20, w19      // local length

        LOAD_SUBADD
        SETUP_LUT       \no_perm

32:
        SETUP_SR_RECOMB 32, x7, x8, x9

        LOAD_INPUT      0,  1,  2,  3,  x2, \no_perm
        LOAD_INPUT      4,  6,  5,  7,  x2, \no_perm, 1
        LOAD_INPUT      8,  9,  10, 11, x2, \no_perm
        LOAD_INPUT      12, 13, 14, 15, x2, \no_perm

        FFT8_X2         v8, v9, v10, v11, v12, v13, v14, v15
        FFT16           v0, v1, v2, v3, v4, v6, v5, v7

        SR_COMBINE      v0,  v1,  v2,  v3,  v4,  v6,  v5,  v7,  \
                        v8,  v9,  v10, v11, v12, v13, v14, v15, \
                        x7,  x8,  x9,  0

        stp             q2,  q3,  [x1, #32*1]
        stp             q6,  q7,  [x1, #32*3]
        stp             q10, q11, [x1, #32*5]
        stp             q14, q15, [x1, #32*7]

        cmp             w20, #32
        b.gt            64f

        stp             q0,  q1,  [x1, #32*0]
        stp             q4,  q5,  [x1, #32*2]
        stp             q8,  q9,  [x1, #32*4]
        stp             q12, q13, [x1, #32*6]

        ret
64:
        SETUP_SR_RECOMB 64, x7, x8, x9

        LOAD_INPUT      2,  3,  10, 11, x2, \no_perm, 1
        LOAD_INPUT      6,  14, 7,  15, x2, \no_perm, 1

        FFT16           v2, v3, v10, v11, v6, v14, v7, v15

        LOAD_INPUT      16, 17, 18, 19, x2, \no_perm
        LOAD_INPUT      20, 22, 21, 23, x2, \no_perm, 1

        FFT16           v16, v17, v18, v19, v20, v22, v21, v23, \
                        v24, v25, v26, v27, v28, v29, v30

        ld1             { v26.4s, v27.4s }, [x8], x9
        ldp             q24, q25, [x7], #32

        ext             v26.16b, v26.16b, v26.16b, #8
        ext             v27.16b, v27.16b, v27.16b, #8

        cmp             w19, #64
        b.eq            2f // custom deinterleave

        // TODO: investigate doing the 2 combines like in deinterleave
        // TODO: experiment with spilling to gprs and converting to HALF or full
        SR_COMBINE_LITE v0,  v1,  v8,  v9,  \
                        v2,  v3,  v16, v17, \
                        v24, v25, v26, v27, \
                        v28, v29, v30, 0

        stp             q0,  q1,  [x1, #32* 0]
        stp             q8,  q9,  [x1, #32* 4]
        stp             q2,  q3,  [x1, #32* 8]
        stp             q16, q17, [x1, #32*12]

        SR_COMBINE_HALF v4,  v5,  v12, v13, \
                        v6,  v7,  v20, v21, \
                        v24, v25, v26, v27, \
                        v28, v29, v30, v0, v1, v8, 1

        stp             q4,  q20, [x1, #32* 2]
        stp             q12, q21, [x1, #32* 6]
        stp             q6,  q5,  [x1, #32*10]
        stp             q7,  q13, [x1, #32*14]

        ldp             q2,  q3,  [x1, #32*1]
        ldp             q6,  q7,  [x1, #32*3]
        ldp             q12, q13, [x1, #32*5]
        ldp             q16, q17, [x1, #32*7]

        SR_COMBINE      v2,  v3,  v12, v13, v6,  v16, v7,  v17, \
                        v10, v11, v14, v15, v18, v19, v22, v23, \
                        x7,  x8,  x9,  0, \
                        v24, v25, v26, v27, v28, v29, v30, v8, v0, v1, v4, v5

        stp             q2,  q3,  [x1, #32* 1]
        stp             q6,  q7,  [x1, #32* 3]
        stp             q12, q13, [x1, #32* 5]
        stp             q16, q17, [x1, #32* 7]

        stp             q10, q11, [x1, #32* 9]
        stp             q18, q19, [x1, #32*11]
        stp             q14, q15, [x1, #32*13]
        stp             q22, q23, [x1, #32*15]

        cmp             w20, #64
        b.gt            128f
        ret
128:
        stp             x20, x30, [sp, #-16]!
        mov             w20, #32
        add             x1, x1, #16*32
        bl              32b
        add             x1, x1, #8*32
        bl              32b
        ldp             x20, x30, [sp], #16
        sub             x1, x1, #24*32

        SETUP_SR_RECOMB 128, x7, x8, x9

        cmp             w19, #128
        b.eq            0f

        SR_COMBINE_FULL 128

        cmp             w20, #128
        b.gt            256f
        ret
256:
        stp             x20, x30, [sp, #-16]!
        mov             w20, #64
        add             x1, x1, #32*32
        bl              32b
        add             x1, x1, #16*32
        bl              32b
        ldp             x20, x30, [sp], #16
        sub             x1, x1, #48*32

        SETUP_SR_RECOMB 256, x7, x8, x9

        cmp             w19, #256
        b.eq            0f

        SR_COMBINE_FULL 256
        SR_COMBINE_FULL 256, 8*32

        cmp             w20, #256
        b.gt            512f
        ret
512:
        stp             x20, x30, [sp, #-16]!
        mov             w20, #128
        add             x1, x1, #64*32
        bl              32b
        add             x1, x1, #32*32
        bl              32b
        ldp             x20, x30, [sp], #16
        sub             x1, x1, #96*32

        SETUP_SR_RECOMB 512, x7, x8, x9

        cmp             w19, #512
        b.eq            0f

        mov             x5, 4
5125:
        SR_COMBINE_FULL 512
        add             x1, x1, 8*32
        subs            w5, w5, 1
        b.gt            5125b

        cmp             w20, #512
        b.gt            1024f

        ret
1024:
        stp             x20, x30, [sp, #-16]!
        mov             w20, #256
        add             x1, x1, #96*32
        bl              32b
        add             x1, x1, #64*32
        bl              32b
        ldp             x20, x30, [sp], #16
        mov             x5, #192*32
        sub             x1, x1, x5

        SETUP_SR_RECOMB 1024, x7, x8, x9

        cmp             w19, #1024
        b.eq            0f

        mov             w5, 8
10245:
        SR_COMBINE_FULL 1024
        add             x1, x1, 8*32
        subs            w5, w5, 1
        b.gt            10245b

        cmp             w20, #1024
        b.gt            2048f

        ret

SR_TRANSFORM_DEF        2048, 4096
SR_TRANSFORM_DEF        4096, 8192
SR_TRANSFORM_DEF        8192, 16384
SR_TRANSFORM_DEF        16384, 32768
SR_TRANSFORM_DEF        32768, 65536
SR_TRANSFORM_DEF        65536, 131072
SR_TRANSFORM_DEF        131072

0: // general deinterleave loop
        SR_COMBINE_DINT
        add             x1, x1, #32*8
        subs            w19, w19, #32*4
        b.gt            0b

        ldp             x19, x20, [sp, #16]
        ldp             d14, d15, [sp, #16*2]
        ldp             d12, d13, [sp, #16*3]
        ldp             d10, d11, [sp, #16*4]
        ldp             d8,  d9,  [sp, #16*5]
        ldp             x21, x22, [sp], #16*6

        ret

2: // special case for 64 point deinterleave
        mov             x10, v23.d[0]
        mov             x11, v23.d[1]

        SR_COMBINE_LITE v0,  v1,  v8,  v9,  \
                        v2,  v3,  v16, v17, \
                        v24, v25, v26, v27, \
                        v28, v29, v30, 0

        SR_COMBINE_HALF v4,  v5,  v12, v13, \
                        v6,  v7,  v20, v21, \
                        v24, v25, v26, v27, \
                        v28, v29, v30, v23, v24, v26, 1

        zip1            v23.2d, v0.2d, v4.2d
        zip2            v24.2d, v0.2d, v4.2d
        zip1            v25.2d, v1.2d, v20.2d
        zip2            v26.2d, v1.2d, v20.2d

        zip1            v27.2d, v8.2d, v12.2d
        zip2            v28.2d, v8.2d, v12.2d
        zip1            v29.2d, v9.2d, v21.2d
        zip2            v30.2d, v9.2d, v21.2d

        mov             v20.16b, v5.16b
        mov             v21.16b, v7.16b
        mov             x12, x1
        add             x13, x1, #32* 4
        add             x14, x1, #32* 8
        add             x15, x1, #32*12

        zip1            v4.2d,  v2.2d, v6.2d
        zip2            v5.2d,  v2.2d, v6.2d
        zip1            v6.2d,  v3.2d, v20.2d
        zip2            v7.2d,  v3.2d, v20.2d

        zip1            v0.2d, v16.2d, v21.2d
        zip2            v1.2d, v16.2d, v21.2d
        zip1            v2.2d, v17.2d, v13.2d
        zip2            v3.2d, v17.2d, v13.2d

        // stp is faster by a little on A53, but this is faster on M1s (theory)
        ldp             q8,  q9, [x1, #32*1]
        ldp             q12, q13, [x1, #32*5]

        st1             { v23.4s, v24.4s, v25.4s, v26.4s }, [x12], #64  // 32* 0...1
        st1             { v27.4s, v28.4s, v29.4s, v30.4s }, [x13], #64  // 32* 4...5
        st1             {  v4.4s,  v5.4s,  v6.4s,  v7.4s }, [x14], #64  // 32* 8...9
        st1             {  v0.4s,  v1.4s,  v2.4s,  v3.4s }, [x15], #64  // 32*12..13

        mov             v23.d[0], x10
        mov             v23.d[1], x11

        ldp             q6,  q7, [x1, #32*3]
        ldp             q16, q17, [x1, #32*7]

        SR_COMBINE      v8,  v9,  v12, v13, v6,  v16, v7,  v17, \
                        v10, v11, v14, v15, v18, v19, v22, v23, \
                        x7,  x8,  x9,  0, \
                        v24, v25, v26, v27, v28, v29, v30, v4, v0, v1, v5, v20

        zip1            v0.2d, v8.2d, v6.2d
        zip2            v1.2d, v8.2d, v6.2d
        zip1            v2.2d, v9.2d, v7.2d
        zip2            v3.2d, v9.2d, v7.2d
        st1             {  v0.4s,  v1.4s,  v2.4s,  v3.4s }, [x12]

        zip1            v4.2d, v12.2d, v16.2d
        zip2            v5.2d, v12.2d, v16.2d
        zip1            v6.2d, v13.2d, v17.2d
        zip2            v7.2d, v13.2d, v17.2d
        st1             {  v4.4s,  v5.4s,  v6.4s,  v7.4s }, [x13]

        zip1            v0.2d, v10.2d, v18.2d
        zip2            v1.2d, v10.2d, v18.2d
        zip1            v2.2d, v11.2d, v19.2d
        zip2            v3.2d, v11.2d, v19.2d
        st1             {  v0.4s,  v1.4s,  v2.4s,  v3.4s }, [x14]

        zip1            v4.2d, v14.2d, v22.2d
        zip2            v5.2d, v14.2d, v22.2d
        zip1            v6.2d, v15.2d, v23.2d
        zip2            v7.2d, v15.2d, v23.2d
        st1             {  v4.4s,  v5.4s,  v6.4s,  v7.4s }, [x15]

        ldp             x19, x20, [sp, #16]
        ldp             d14, d15, [sp, #16*2]
        ldp             d12, d13, [sp, #16*3]
        ldp             d10, d11, [sp, #16*4]
        ldp             d8,  d9,  [sp, #16*5]
        ldp             x21, x22, [sp], #16*6

        ret
endfunc
.endm

FFT_SPLIT_RADIX_FN float, 0
FFT_SPLIT_RADIX_FN ns_float, 1

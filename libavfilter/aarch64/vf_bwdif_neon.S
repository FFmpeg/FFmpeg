/*
 * bwdif aarch64 NEON optimisations
 *
 * Copyright (c) 2023 John Cox <jc@kynesim.co.uk>
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

// Space taken on the stack by an int (32-bit)
#ifdef __APPLE__
.set    SP_INT, 4
#else
.set    SP_INT, 8
#endif

.macro SQSHRUNN b, s0, s1, s2, s3, n
        sqshrun         \s0\().4h, \s0\().4s, #\n - 8
        sqshrun2        \s0\().8h, \s1\().4s, #\n - 8
        sqshrun         \s1\().4h, \s2\().4s, #\n - 8
        sqshrun2        \s1\().8h, \s3\().4s, #\n - 8
        uzp2            \b\().16b, \s0\().16b, \s1\().16b
.endm

.macro SMULL4K a0, a1, a2, a3, s0, s1, k
        smull           \a0\().4s, \s0\().4h, \k
        smull2          \a1\().4s, \s0\().8h, \k
        smull           \a2\().4s, \s1\().4h, \k
        smull2          \a3\().4s, \s1\().8h, \k
.endm

.macro UMULL4K a0, a1, a2, a3, s0, s1, k
        umull           \a0\().4s, \s0\().4h, \k
        umull2          \a1\().4s, \s0\().8h, \k
        umull           \a2\().4s, \s1\().4h, \k
        umull2          \a3\().4s, \s1\().8h, \k
.endm

.macro UMLAL4K a0, a1, a2, a3, s0, s1, k
        umlal           \a0\().4s, \s0\().4h, \k
        umlal2          \a1\().4s, \s0\().8h, \k
        umlal           \a2\().4s, \s1\().4h, \k
        umlal2          \a3\().4s, \s1\().8h, \k
.endm

.macro UMLSL4K a0, a1, a2, a3, s0, s1, k
        umlsl           \a0\().4s, \s0\().4h, \k
        umlsl2          \a1\().4s, \s0\().8h, \k
        umlsl           \a2\().4s, \s1\().4h, \k
        umlsl2          \a3\().4s, \s1\().8h, \k
.endm

//      int b = m2s1 - m1;
//      int f = p2s1 - p1;
//      int dc = c0s1 - m1;
//      int de = c0s1 - p1;
//      int sp_max = FFMIN(p1 - c0s1, m1 - c0s1);
//      sp_max = FFMIN(sp_max, FFMAX(-b,-f));
//      int sp_min = FFMIN(c0s1 - p1, c0s1 - m1);
//      sp_min = FFMIN(sp_min, FFMAX(b,f));
//      diff = diff == 0 ? 0 : FFMAX3(diff, sp_min, sp_max);
.macro SPAT_CHECK diff, m2s1, m1, c0s1, p1, p2s1, t0, t1, t2, t3
        uqsub           \t0\().16b, \p1\().16b, \c0s1\().16b
        uqsub           \t2\().16b, \m1\().16b, \c0s1\().16b
        umin            \t2\().16b, \t0\().16b, \t2\().16b

        uqsub           \t1\().16b, \m1\().16b, \m2s1\().16b
        uqsub           \t3\().16b, \p1\().16b, \p2s1\().16b
        umax            \t3\().16b, \t3\().16b, \t1\().16b
        umin            \t3\().16b, \t3\().16b, \t2\().16b

        uqsub           \t0\().16b, \c0s1\().16b, \p1\().16b
        uqsub           \t2\().16b, \c0s1\().16b, \m1\().16b
        umin            \t2\().16b, \t0\().16b, \t2\().16b

        uqsub           \t1\().16b, \m2s1\().16b, \m1\().16b
        uqsub           \t0\().16b, \p2s1\().16b, \p1\().16b
        umax            \t0\().16b, \t0\().16b, \t1\().16b
        umin            \t2\().16b, \t2\().16b, \t0\().16b

        cmeq            \t1\().16b, \diff\().16b, #0
        umax            \diff\().16b, \diff\().16b, \t3\().16b
        umax            \diff\().16b, \diff\().16b, \t2\().16b
        bic             \diff\().16b, \diff\().16b, \t1\().16b
.endm

//      i0 = s0;
//      if (i0 > d0 + diff0)
//          i0 = d0 + diff0;
//      else if (i0 < d0 - diff0)
//          i0 = d0 - diff0;
//
// i0 = s0 is safe
.macro DIFF_CLIP i0, s0, d0, diff, t0, t1
        uqadd           \t0\().16b, \d0\().16b, \diff\().16b
        uqsub           \t1\().16b, \d0\().16b, \diff\().16b
        umin            \i0\().16b, \s0\().16b, \t0\().16b
        umax            \i0\().16b, \i0\().16b, \t1\().16b
.endm

//      i0 = FFABS(m1 - p1) > td0 ? i1 : i2;
//      DIFF_CLIP
//
// i0 = i1 is safe
.macro INTERPOL i0, i1, i2, m1, d0, p1, td0, diff, t0, t1, t2
        uabd            \t0\().16b, \m1\().16b, \p1\().16b
        cmhi            \t0\().16b, \t0\().16b, \td0\().16b
        bsl             \t0\().16b, \i1\().16b, \i2\().16b
        DIFF_CLIP       \i0, \t0, \d0, \diff, \t1, \t2
.endm

.macro PUSH_VREGS
        stp             d8,  d9,  [sp, #-64]!
        stp             d10, d11, [sp, #16]
        stp             d12, d13, [sp, #32]
        stp             d14, d15, [sp, #48]
.endm

.macro POP_VREGS
        ldp             d14, d15, [sp, #48]
        ldp             d12, d13, [sp, #32]
        ldp             d10, d11, [sp, #16]
        ldp             d8,  d9,  [sp], #64
.endm

.macro LDR_COEFFS d, t0
        movrel          \t0, coeffs, 0
        ld1             {\d\().8h}, [\t0]
.endm

// static const uint16_t coef_lf[2] = { 4309, 213 };
// static const uint16_t coef_hf[3] = { 5570, 3801, 1016 };
// static const uint16_t coef_sp[2] = { 5077, 981 };

const coeffs, align=4   // align 4 means align on 2^4 boundry
        .hword          4309 * 4, 213 * 4               // lf[0]*4 = v0.h[0]
        .hword          5570, 3801, 1016, -3801         // hf[0] = v0.h[2], -hf[1] = v0.h[5]
        .hword          5077, 981                       // sp[0] = v0.h[6]
endconst

// ===========================================================================
//
// void ff_bwdif_filter_line3_neon(
//         void * dst1,         // x0
//         int d_stride,        // w1
//         const void * prev1,  // x2
//         const void * cur1,   // x3
//         const void * next1,  // x4
//         int s_stride,        // w5
//         int w,               // w6
//         int parity,          // w7
//         int clip_max);       // [sp, #0] (Ignored)

function ff_bwdif_filter_line3_neon, export=1
        // Sanity check w
        cmp             w6, #0
        ble             99f

        LDR_COEFFS      v0, x17

// #define prev2 cur
//        const uint8_t * restrict next2 = parity ? prev : next;
        cmp             w7, #0
        csel            x17, x2, x4, ne

        // We want all the V registers - save all the ones we must
        PUSH_VREGS

        // Some rearrangement of initial values for nice layout of refs in regs
        mov             w10, w6                         // w10 = loop count
        neg             w9,  w5                         // w9  = mref
        lsl             w8,  w9,  #1                    // w8 =  mref2
        add             w7,  w9,  w9, lsl #1            // w7  = mref3
        lsl             w6,  w9,  #2                    // w6  = mref4
        mov             w11, w5                         // w11 = pref
        lsl             w12, w5,  #1                    // w12 = pref2
        add             w13, w5,  w5, lsl #1            // w13 = pref3
        lsl             w14, w5,  #2                    // w14 = pref4
        add             w15, w5,  w5, lsl #2            // w15 = pref5
        add             w16, w14, w12                   // w16 = pref6

        lsl             w5,  w1,  #1                    // w5 = d_stride * 2

//         for (x = 0; x < w; x++) {
//             int diff0, diff2;
//             int d0, d2;
//             int temporal_diff0, temporal_diff2;
//
//             int i1, i2;
//             int j1, j2;
//             int p6, p5, p4, p3, p2, p1, c0, m1, m2, m3, m4;

10:
//             c0 = prev2[0] + next2[0];                // c0 = v20, v21
//             d0  = c0 >> 1;                           // d0 = v10
//             temporal_diff0 = FFABS(prev2[0] - next2[0]); // td0 = v11
        ldr             q31, [x3]
        ldr             q21, [x17]
        uhadd           v10.16b, v31.16b, v21.16b
        uabd            v11.16b, v31.16b, v21.16b
        uaddl           v20.8h,  v21.8b,  v31.8b
        uaddl2          v21.8h,  v21.16b, v31.16b

        ldr             q31, [x3, w6, sxtw]
        ldr             q23, [x17, w6, sxtw]

//             i1 = coef_hf[0] * c0;                    // i1 = v2-v5
        UMULL4K         v2, v3, v4, v5, v20, v21, v0.h[2]

        ldr             q30, [x3, w14, sxtw]
        ldr             q25, [x17, w14, sxtw]

//             m4 = prev2[mrefs4] + next2[mrefs4];      // m4 = v22,v23
        uaddl           v22.8h,  v23.8b,  v31.8b
        uaddl2          v23.8h,  v23.16b, v31.16b

//             p4 = prev2[prefs4] + next2[prefs4];      // p4 = v24,v25, (p4 >> 1) = v12
        uhadd           v12.16b, v25.16b, v30.16b
        uaddl           v24.8h,  v25.8b,  v30.8b
        uaddl2          v25.8h,  v25.16b, v30.16b

//             j1 = -coef_hf[1] * (c0 + p4);            // j1 = v6-v9  (-c0:v20,v21)
        add             v20.8h,  v20.8h,  v24.8h
        add             v21.8h,  v21.8h,  v25.8h
        SMULL4K         v6, v7, v8, v9, v20, v21, v0.h[5]

//             m3 = cur[mrefs3];                        // m3 = v20
        ldr             q20, [x3, w7, sxtw]

//             p3 = cur[prefs3];                        // p3 = v21
        ldr             q21, [x3, w13, sxtw]

//             i1 += coef_hf[2] * (m4 + p4);            // (-m4:v22,v23) (-p4:v24,v25)
        add             v22.8h,  v22.8h,  v24.8h
        add             v23.8h,  v23.8h,  v25.8h
        UMLAL4K         v2, v3, v4, v5, v22, v23, v0.h[4]

        ldr             q29, [x3, w8, sxtw]
        ldr             q23, [x17, w8, sxtw]

//             i1 -= coef_lf[1] * 4 * (m3 + p3);        // -
        uaddl           v30.8h,  v20.8b,  v21.8b
        uaddl2          v31.8h,  v20.16b, v21.16b

        ldr             q28, [x3, w16, sxtw]
        ldr             q25, [x17, w16, sxtw]

        UMLSL4K         v2, v3, v4, v5, v30, v31, v0.h[1]

//             m2 = prev2[mrefs2] + next2[mrefs2];      // m2 = v22,v23, (m2 >> 1) = v13
        uhadd           v13.16b, v23.16b, v29.16b
        uaddl           v22.8h,  v23.8b,  v29.8b
        uaddl2          v23.8h,  v23.16b, v29.16b

        ldr             q31, [x3, w12, sxtw]
        ldr             q27, [x17, w12, sxtw]

//             p6 = prev2[prefs6] + next2[prefs6];      // p6 = v24,v25
        uaddl           v24.8h,  v25.8b,  v28.8b
        uaddl2          v25.8h,  v25.16b, v28.16b

//             j1 += coef_hf[2] * (m2 + p6);            // (-p6:v24,v25)
        add             v24.8h,  v24.8h,  v22.8h
        add             v25.8h,  v25.8h,  v23.8h
        UMLAL4K         v6, v7, v8, v9, v24, v25, v0.h[4]

//             m1 = cur[mrefs];                         // m1 = v24
        ldr             q24, [x3, w9, sxtw]

//             p5 = cur[prefs5];                        // p5 = v25
        ldr             q25, [x3, w15, sxtw]

//             p2 = prev2[prefs2] + next2[prefs2];      // p2 = v26, v27
//             temporal_diff2 = FFABS(prev2[prefs2] - next2[prefs2]); // td2 = v14
//             d2  = p2 >> 1;                           // d2 = v15
        uabd            v14.16b, v31.16b, v27.16b
        uhadd           v15.16b, v31.16b, v27.16b
        uaddl           v26.8h,  v27.8b,  v31.8b
        uaddl2          v27.8h,  v27.16b, v31.16b

//             j1 += coef_hf[0] * p2;                   // -
        UMLAL4K         v6, v7, v8, v9, v26, v27, v0.h[2]

//             i1 -= coef_hf[1] * (m2 + p2);            // (-m2:v22,v23*) (-p2:v26*,v27*)
        add             v22.8h,  v22.8h,  v26.8h
        add             v23.8h,  v23.8h,  v27.8h
        UMLSL4K         v2, v3, v4, v5, v22, v23, v0.h[3]

//             p1 = cur[prefs];                         // p1 = v22
        ldr             q22, [x3, w11, sxtw]

//             j1 -= coef_lf[1] * 4 * (m1 + p5);        // -
        uaddl           v26.8h,  v24.8b,  v25.8b
        uaddl2          v27.8h,  v24.16b, v25.16b
        UMLSL4K         v6, v7, v8, v9, v26, v27, v0.h[1]

//             j2 = (coef_sp[0] * (p1 + p3) - coef_sp[1]  * (m1 + p5)) >> 13; // (-p5:v25*) j2=v16
        uaddl           v18.8h,  v22.8b,  v21.8b
        uaddl2          v19.8h,  v22.16b, v21.16b
        UMULL4K         v28, v29, v30, v31, v18, v19, v0.h[6]

        uaddl           v18.8h,  v24.8b,  v25.8b
        uaddl2          v19.8h,  v24.16b, v25.16b
        UMLSL4K         v28, v29, v30, v31, v18, v19, v0.h[7]

        SQSHRUNN        v16, v28, v29, v30, v31, 13

//             i2 = (coef_sp[0] * (m1 + p1) - coef_sp[1] * (m3 + p3)) >> 13; // (-m3:v20*) i2=v17
        uaddl           v18.8h,  v22.8b,  v24.8b
        uaddl2          v19.8h,  v22.16b, v24.16b
        UMULL4K         v28, v29, v30, v31, v18, v19, v0.h[6]

        uaddl           v18.8h,  v20.8b,  v21.8b
        uaddl2          v19.8h,  v20.16b, v21.16b
        UMLSL4K         v28, v29, v30, v31, v18, v19, v0.h[7]

        SQSHRUNN        v17, v28, v29, v30, v31, 13

//             i1 += coef_lf[0] * 4 * (m1 + p1);        // p1 = v22, m1 = v24
        uaddl           v26.8h,  v24.8b,  v22.8b
        uaddl2          v27.8h,  v24.16b, v22.16b
        UMLAL4K         v2, v3, v4, v5, v26, v27, v0.h[0]

        ldr             q31, [x2, w9, sxtw]
        ldr             q29, [x4, w9, sxtw]

//             j1 += coef_lf[0] * 4 * (p1 + p3);        // p1 = v22, p3 = v21
        uaddl           v26.8h,  v21.8b,  v22.8b
        uaddl2          v27.8h,  v21.16b, v22.16b
        UMLAL4K         v6, v7, v8, v9, v26, v27, v0.h[0]

        ldr             q30, [x2, w11, sxtw]
        ldr             q28, [x4, w11, sxtw]

//             i1 >>= 15;                               // i1 = v2, -v3, -v4*, -v5*
        SQSHRUNN        v2, v2, v3, v4, v5, 15

//             j1 >>= 15;                               // j1 = v3, -v6*, -v7*, -v8*, -v9*
        SQSHRUNN        v3, v6, v7, v8, v9, 15

//             {
//                 int t1 =(FFABS(prev[mrefs] - m1) + FFABS(prev[prefs] - p1)) >> 1;
//                 int t2 =(FFABS(next[mrefs] - m1) + FFABS(next[prefs] - p1)) >> 1;
        uabd            v30.16b, v22.16b, v30.16b
        uabd            v31.16b, v24.16b, v31.16b
        uabd            v28.16b, v22.16b, v28.16b
        uabd            v29.16b, v24.16b, v29.16b
        uhadd           v31.16b, v31.16b, v30.16b
        uhadd           v29.16b, v29.16b, v28.16b

        ldr             q27, [x2, w13, sxtw]
        ldr             q26, [x4, w13, sxtw]

//                 diff0 = FFMAX3(temporal_diff0 >> 1, t1, t2); // diff0=v18
        ushr            v18.16b, v11.16b, #1
        umax            v18.16b, v18.16b, v31.16b
        umax            v18.16b, v18.16b, v29.16b
//             }                                        // v28, v30 preserved for next block
//             {  // tdiff2 = v14
//                 int t1 =(FFABS(prev[prefs] - p1) + FFABS(prev[prefs3] - p3)) >> 1;
//                 int t2 =(FFABS(next[prefs] - p1) + FFABS(next[prefs3] - p3)) >> 1;
        uabd            v31.16b, v21.16b, v27.16b
        uabd            v29.16b, v21.16b, v26.16b
        uhadd           v31.16b, v31.16b, v30.16b
        uhadd           v29.16b, v29.16b, v28.16b

//                 diff2 = FFMAX3(temporal_diff2 >> 1, t1, t2); // diff2=v19
        ushr            v19.16b, v14.16b, #1
        umax            v19.16b, v19.16b, v31.16b
        umax            v19.16b, v19.16b, v29.16b
//             }

        // diff0 = v18, (m2 >> 1) = v13, m1 = v24, d0 = v10, p1 = v22, d2 = v15
        SPAT_CHECK      v18, v13, v24, v10, v22, v15, v31, v30, v29, v28

        //  diff2 = v19, d0 = v10, p1 = v22, d2 = v15, p3 = v21, (p4 >> 1) = v12
        SPAT_CHECK      v19, v10, v22, v15, v21, v12, v31, v30, v29, v28

        // j1 = v3, j2 = v16, p1 = v22, d2 = v15, p3 = v21, td2 = v14, diff2 = v19
        INTERPOL        v3, v3, v16, v22, v15, v21, v14, v19, v31, v30, v29

//                 dst[d_stride * 2] = av_clip_uint8(interpol);
        str             q3,  [x0, w5, sxtw]

//             dst[d_stride] = p1;
        str             q22, [x0, w1, sxtw]

        // i1 = v2, i2 = v17, m1 = v24, d0 = v10, p1 = v22, td2 = v11, diff2 = v18
        INTERPOL        v2, v2, v17, v24, v10, v22, v11, v18, v31, v30, v29

//                 dst[0] = av_clip_uint8(interpol);
        str             q2,  [x0], #16
//             }
//
//             dst++;
//             cur++;
//             prev++;
//             prev2++;
//             next++;
//         }
        subs            w10, w10, #16
        add             x2,  x2,  #16
        add             x3,  x3,  #16
        add             x4,  x4,  #16
        add             x17, x17, #16
        bgt             10b

        POP_VREGS
99:
        ret
endfunc

// ===========================================================================
//
// void filter_line(
//      void *dst1,     // x0
//      void *prev1,    // x1
//      void *cur1,     // x2
//      void *next1,    // x3
//      int w,          // w4
//      int prefs,      // w5
//      int mrefs,      // w6
//      int prefs2,     // w7
//      int mrefs2,     // [sp, #0]
//      int prefs3,     // [sp, #SP_INT]
//      int mrefs3,     // [sp, #SP_INT*2]
//      int prefs4,     // [sp, #SP_INT*3]
//      int mrefs4,     // [sp, #SP_INT*4]
//      int parity,     // [sp, #SP_INT*5]
//      int clip_max)   // [sp, #SP_INT*6]

function ff_bwdif_filter_line_neon, export=1
        // Sanity check w
        cmp             w4, #0
        ble             99f

        // Rearrange regs to be the same as line3 for ease of debug!
        mov             w10, w4                         // w10 = loop count
        mov             w9,  w6                         // w9  = mref
        mov             w12, w7                         // w12 = pref2
        mov             w11, w5                         // w11 = pref
        ldr             w8,  [sp, #0]                   // w8 =  mref2
        ldr             w7,  [sp, #SP_INT*2]            // w7  = mref3
        ldr             w6,  [sp, #SP_INT*4]            // w6  = mref4
        ldr             w13, [sp, #SP_INT]              // w13 = pref3
        ldr             w14, [sp, #SP_INT*3]            // w14 = pref4

        mov             x4,  x3
        mov             x3,  x2
        mov             x2,  x1

        LDR_COEFFS      v0, x17

// #define prev2 cur
//        const uint8_t * restrict next2 = parity ? prev : next;
        ldr             w17, [sp, #SP_INT*5]            // parity
        cmp             w17, #0
        csel            x17, x2, x4, ne

        PUSH_VREGS

//         for (x = 0; x < w; x++) {
//             int diff0, diff2;
//             int d0, d2;
//             int temporal_diff0, temporal_diff2;
//
//             int i1, i2;
//             int j1, j2;
//             int p6, p5, p4, p3, p2, p1, c0, m1, m2, m3, m4;

10:
//             c0 = prev2[0] + next2[0];            // c0 = v20, v21
//             d0  = c0 >> 1;                       // d0 = v10
//             temporal_diff0 = FFABS(prev2[0] - next2[0]); // td0 = v11
        ldr             q31, [x3]
        ldr             q21, [x17]
        uhadd           v10.16b, v31.16b, v21.16b
        uabd            v11.16b, v31.16b, v21.16b
        uaddl           v20.8h,  v21.8b,  v31.8b
        uaddl2          v21.8h,  v21.16b, v31.16b

        ldr             q31, [x3, w6, sxtw]
        ldr             q23, [x17, w6, sxtw]

//             i1 = coef_hf[0] * c0;                // i1 = v2-v5
        UMULL4K         v2, v3, v4, v5, v20, v21, v0.h[2]

        ldr             q30, [x3, w14, sxtw]
        ldr             q25, [x17, w14, sxtw]

//             m4 = prev2[mrefs4] + next2[mrefs4];  // m4 = v22,v23
        uaddl           v22.8h,  v23.8b,  v31.8b
        uaddl2          v23.8h,  v23.16b, v31.16b

//             p4 = prev2[prefs4] + next2[prefs4];  // p4 = v24,v25, (p4 >> 1) = v12
        uhadd           v12.16b, v25.16b, v30.16b
        uaddl           v24.8h,  v25.8b,  v30.8b
        uaddl2          v25.8h,  v25.16b, v30.16b

//             m3 = cur[mrefs3];                    // m3 = v20
        ldr             q20, [x3, w7, sxtw]

//             p3 = cur[prefs3];                    // p3 = v21
        ldr             q21, [x3, w13, sxtw]

//             i1 += coef_hf[2] * (m4 + p4);        // (-m4:v22,v23) (-p4:v24,v25)
        add             v22.8h,  v22.8h,  v24.8h
        add             v23.8h,  v23.8h,  v25.8h
        UMLAL4K         v2, v3, v4, v5, v22, v23, v0.h[4]

        ldr             q29, [x3, w8, sxtw]
        ldr             q23, [x17, w8, sxtw]

//             i1 -= coef_lf[1] * 4 * (m3 + p3);    // -
        uaddl           v30.8h,  v20.8b,  v21.8b
        uaddl2          v31.8h,  v20.16b, v21.16b

        UMLSL4K         v2, v3, v4, v5, v30, v31, v0.h[1]

        ldr             q31, [x3, w12, sxtw]
        ldr             q27, [x17, w12, sxtw]

//             m2 = prev2[mrefs2] + next2[mrefs2];  // m2 = v22,v23, (m2 >> 1) = v13
        uhadd           v13.16b, v23.16b, v29.16b
        uaddl           v22.8h,  v23.8b,  v29.8b
        uaddl2          v23.8h,  v23.16b, v29.16b

//             m1 = cur[mrefs];                     // m1 = v24
        ldr             q24, [x3, w9, sxtw]

//             p2 = prev2[prefs2] + next2[prefs2];  // p2 = v26, v27
//             temporal_diff2 = FFABS(prev2[prefs2] - next2[prefs2]); // td2 = v14
//             d2  = p2 >> 1;                       // d2 = v15
        uabd            v14.16b, v31.16b, v27.16b
        uhadd           v15.16b, v31.16b, v27.16b
        uaddl           v26.8h,  v27.8b,  v31.8b
        uaddl2          v27.8h,  v27.16b, v31.16b

//             i1 -= coef_hf[1] * (m2 + p2);        // (-m2:v22,v23*) (-p2:v26*,v27*)
        add             v22.8h,  v22.8h,  v26.8h
        add             v23.8h,  v23.8h,  v27.8h
        UMLSL4K         v2, v3, v4, v5, v22, v23, v0.h[3]

//             p1 = cur[prefs];                     // p1 = v22
        ldr             q22, [x3, w11, sxtw]

//             i2 = (coef_sp[0] * (m1 + p1) - coef_sp[1] * (m3 + p3)) >> 13; // (-m3:v20*) i2=v17
        uaddl           v18.8h,  v22.8b,  v24.8b
        uaddl2          v19.8h,  v22.16b, v24.16b
        UMULL4K         v28, v29, v30, v31, v18, v19, v0.h[6]

        uaddl           v18.8h,  v20.8b,  v21.8b
        uaddl2          v19.8h,  v20.16b, v21.16b
        UMLSL4K         v28, v29, v30, v31, v18, v19, v0.h[7]

        SQSHRUNN        v17, v28, v29, v30, v31, 13

//             i1 += coef_lf[0] * 4 * (m1 + p1);    // p1 = v22, m1 = v24
        uaddl           v26.8h,  v24.8b,  v22.8b
        uaddl2          v27.8h,  v24.16b, v22.16b
        UMLAL4K         v2, v3, v4, v5, v26, v27, v0.h[0]

        ldr             q31, [x2, w9, sxtw]
        ldr             q29, [x4, w9, sxtw]

        ldr             q30, [x2, w11, sxtw]
        ldr             q28, [x4, w11, sxtw]

//             i1 >>= 15;                            // i1 = v2, -v3, -v4*, -v5*
        SQSHRUNN        v2, v2, v3, v4, v5, 15

//             {
//                 int t1 =(FFABS(prev[mrefs] - m1) + FFABS(prev[prefs] - p1)) >> 1;
//                 int t2 =(FFABS(next[mrefs] - m1) + FFABS(next[prefs] - p1)) >> 1;
        uabd            v30.16b, v22.16b, v30.16b
        uabd            v31.16b, v24.16b, v31.16b
        uabd            v28.16b, v22.16b, v28.16b
        uabd            v29.16b, v24.16b, v29.16b
        uhadd           v31.16b, v31.16b, v30.16b
        uhadd           v29.16b, v29.16b, v28.16b

//                 diff0 = FFMAX3(temporal_diff0 >> 1, t1, t2); // diff0=v18
        ushr            v18.16b, v11.16b, #1
        umax            v18.16b, v18.16b, v31.16b
        umax            v18.16b, v18.16b, v29.16b

        // diff0 = v18, (m2 >> 1) = v13, m1 = v24, d0 = v10, p1 = v22, d2 = v15
        SPAT_CHECK      v18, v13, v24, v10, v22, v15, v31, v30, v29, v28

        // i1 = v2, i2 = v17, m1 = v24, d0 = v10, p1 = v22, td2 = v11, diff2 = v18
        INTERPOL        v2, v2, v17, v24, v10, v22, v11, v18, v31, v30, v29

//                 dst[0] = av_clip_uint8(interpol);
        str             q2,  [x0], #16
//             }
//
//             dst++;
//             cur++;
//             prev++;
//             prev2++;
//             next++;
//         }

        subs            w10, w10, #16
        add             x2,  x2,  #16
        add             x3,  x3,  #16
        add             x4,  x4,  #16
        add             x17, x17, #16
        bgt             10b

        POP_VREGS
99:
        ret
endfunc

// ============================================================================
//
// void ff_bwdif_filter_edge_neon(
//      void *dst1,     // x0
//      void *prev1,    // x1
//      void *cur1,     // x2
//      void *next1,    // x3
//      int w,          // w4
//      int prefs,      // w5
//      int mrefs,      // w6
//      int prefs2,     // w7
//      int mrefs2,     // [sp, #0]
//      int parity,     // [sp, #SP_INT]
//      int clip_max,   // [sp, #SP_INT*2]  unused
//      int spat);      // [sp, #SP_INT*3]

function ff_bwdif_filter_edge_neon, export=1
        // Sanity check w
        cmp             w4, #0
        ble             99f

// #define prev2 cur
//     const uint8_t * restrict next2 = parity ? prev : next;

        ldr             w8,  [sp, #0]                   // mrefs2

        ldr             w17, [sp, #SP_INT]              // parity
        ldr             w16, [sp, #SP_INT*3]            // spat
        cmp             w17, #0
        csel            x17, x1, x3, ne

//     for (x = 0; x < w; x++) {

10:
//        int m1 = cur[mrefs];
//        int d = (prev2[0] + next2[0]) >> 1;
//        int p1 = cur[prefs];
//        int temporal_diff0 = FFABS(prev2[0] - next2[0]);
//        int temporal_diff1 =(FFABS(prev[mrefs] - m1) + FFABS(prev[prefs] - p1)) >> 1;
//        int temporal_diff2 =(FFABS(next[mrefs] - m1) + FFABS(next[prefs] - p1)) >> 1;
//        int diff = FFMAX3(temporal_diff0 >> 1, temporal_diff1, temporal_diff2);
        ldr             q31, [x2]
        ldr             q21, [x17]
        uhadd           v16.16b, v31.16b, v21.16b       // d0 = v16
        uabd            v17.16b, v31.16b, v21.16b       // td0 = v17
        ldr             q24, [x2, w6, sxtw]             // m1 = v24
        ldr             q22, [x2, w5, sxtw]             // p1 = v22

        ldr             q0,  [x1, w6, sxtw]             // prev[mrefs]
        ldr             q2,  [x1, w5, sxtw]             // prev[prefs]
        ldr             q1,  [x3, w6, sxtw]             // next[mrefs]
        ldr             q3,  [x3, w5, sxtw]             // next[prefs]

        ushr            v29.16b, v17.16b, #1

        uabd            v31.16b, v0.16b,  v24.16b
        uabd            v30.16b, v2.16b,  v22.16b
        uhadd           v0.16b,  v31.16b, v30.16b       // td1 = q0

        uabd            v31.16b, v1.16b,  v24.16b
        uabd            v30.16b, v3.16b,  v22.16b
        uhadd           v1.16b,  v31.16b, v30.16b       // td2 = q1

        umax            v0.16b,  v0.16b,  v29.16b
        umax            v0.16b,  v0.16b,  v1.16b        // diff = v0

//        if (spat) {
//            SPAT_CHECK()
//        }
//        i0 = (m1 + p1) >> 1;
        cbz             w16, 1f

        ldr             q31, [x2,  w8, sxtw]
        ldr             q18, [x17, w8, sxtw]
        ldr             q30, [x2,  w7, sxtw]
        ldr             q19, [x17, w7, sxtw]
        uhadd           v18.16b, v18.16b, v31.16b
        uhadd           v19.16b, v19.16b, v30.16b

        SPAT_CHECK      v0, v18, v24, v16, v22, v19, v31, v30, v29, v28

1:
        uhadd           v2.16b,  v22.16b, v24.16b

        // i0 = v2, s0 = v2, d0 = v16, diff = v0, t0 = v31, t1 = v30
        DIFF_CLIP       v2, v2, v16, v0, v31, v30

//        dst[0] = av_clip(interpol, 0, clip_max);
        str             q2, [x0], #16

//        dst++;
//        cur++;
//    }
        subs            w4,  w4,  #16
        add             x1,  x1,  #16
        add             x2,  x2,  #16
        add             x3,  x3,  #16
        add             x17, x17, #16
        bgt             10b

99:
        ret
endfunc

// ============================================================================
//
// void ff_bwdif_filter_intra_neon(
//      void *dst1,     // x0
//      void *cur1,     // x1
//      int w,          // w2
//      int prefs,      // w3
//      int mrefs,      // w4
//      int prefs3,     // w5
//      int mrefs3,     // w6
//      int parity,     // w7       unused
//      int clip_max)   // [sp, #0] unused

function ff_bwdif_filter_intra_neon, export=1
        cmp             w2, #0
        ble             99f

        LDR_COEFFS      v0, x17

//    for (x = 0; x < w; x++) {
10:

//        interpol = (coef_sp[0] * (cur[mrefs] + cur[prefs]) - coef_sp[1] * (cur[mrefs3] + cur[prefs3])) >> 13;
        ldr             q31, [x1, w4, sxtw]
        ldr             q30, [x1, w3, sxtw]
        ldr             q29, [x1, w6, sxtw]
        ldr             q28, [x1, w5, sxtw]

        uaddl           v20.8h,  v31.8b,  v30.8b
        uaddl2          v21.8h,  v31.16b, v30.16b

        UMULL4K         v2, v3, v4, v5, v20, v21, v0.h[6]

        uaddl           v20.8h,  v29.8b,  v28.8b
        uaddl2          v21.8h,  v29.16b, v28.16b

        UMLSL4K         v2, v3, v4, v5, v20, v21, v0.h[7]

//        dst[0] = av_clip(interpol, 0, clip_max);
        SQSHRUNN        v2, v2, v3, v4, v5, 13
        str             q2, [x0], #16

//        dst++;
//        cur++;
//    }

        subs            w2,  w2,  #16
        add             x1,  x1,  #16
        bgt             10b

99:
        ret
endfunc

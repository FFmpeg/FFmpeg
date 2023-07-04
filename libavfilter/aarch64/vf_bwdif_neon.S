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

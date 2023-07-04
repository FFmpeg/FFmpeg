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

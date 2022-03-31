/*
 * VC1 AArch64 NEON optimisations
 *
 * Copyright (c) 2022 Ben Avison <bavison@riscosopen.org>
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

.align  5
.Lcoeffs:
.quad   0x00050002

// VC-1 in-loop deblocking filter for 4 pixel pairs at boundary of vertically-neighbouring blocks
// On entry:
//   x0 -> top-left pel of lower block
//   x1 = row stride, bytes
//   w2 = PQUANT bitstream parameter
function ff_vc1_v_loop_filter4_neon, export=1
        sub             x3, x0, w1, sxtw #2
        ldr             d0, .Lcoeffs
        ld1             {v1.s}[0], [x0], x1     // P5
        ld1             {v2.s}[0], [x3], x1     // P1
        ld1             {v3.s}[0], [x3], x1     // P2
        ld1             {v4.s}[0], [x0], x1     // P6
        ld1             {v5.s}[0], [x3], x1     // P3
        ld1             {v6.s}[0], [x0], x1     // P7
        ld1             {v7.s}[0], [x3]         // P4
        ld1             {v16.s}[0], [x0]        // P8
        ushll           v17.8h, v1.8b, #1       // 2*P5
        dup             v18.8h, w2              // pq
        ushll           v2.8h, v2.8b, #1        // 2*P1
        uxtl            v3.8h, v3.8b            // P2
        uxtl            v4.8h, v4.8b            // P6
        uxtl            v19.8h, v5.8b           // P3
        mls             v2.4h, v3.4h, v0.h[1]   // 2*P1-5*P2
        uxtl            v3.8h, v6.8b            // P7
        mls             v17.4h, v4.4h, v0.h[1]  // 2*P5-5*P6
        ushll           v5.8h, v5.8b, #1        // 2*P3
        uxtl            v6.8h, v7.8b            // P4
        mla             v17.4h, v3.4h, v0.h[1]  // 2*P5-5*P6+5*P7
        uxtl            v3.8h, v16.8b           // P8
        mla             v2.4h, v19.4h, v0.h[1]  // 2*P1-5*P2+5*P3
        uxtl            v1.8h, v1.8b            // P5
        mls             v5.4h, v6.4h, v0.h[1]   // 2*P3-5*P4
        mls             v17.4h, v3.4h, v0.h[0]  // 2*P5-5*P6+5*P7-2*P8
        sub             v3.4h, v6.4h, v1.4h     // P4-P5
        mls             v2.4h, v6.4h, v0.h[0]   // 2*P1-5*P2+5*P3-2*P4
        mla             v5.4h, v1.4h, v0.h[1]   // 2*P3-5*P4+5*P5
        mls             v5.4h, v4.4h, v0.h[0]   // 2*P3-5*P4+5*P5-2*P6
        abs             v4.4h, v3.4h
        srshr           v7.4h, v17.4h, #3
        srshr           v2.4h, v2.4h, #3
        sshr            v4.4h, v4.4h, #1        // clip
        srshr           v5.4h, v5.4h, #3
        abs             v7.4h, v7.4h            // a2
        sshr            v3.4h, v3.4h, #8        // clip_sign
        abs             v2.4h, v2.4h            // a1
        cmeq            v16.4h, v4.4h, #0       // test clip == 0
        abs             v17.4h, v5.4h           // a0
        sshr            v5.4h, v5.4h, #8        // a0_sign
        cmhs            v19.4h, v2.4h, v7.4h    // test a1 >= a2
        cmhs            v18.4h, v17.4h, v18.4h  // test a0 >= pq
        sub             v3.4h, v3.4h, v5.4h     // clip_sign - a0_sign
        bsl             v19.8b, v7.8b, v2.8b    // a3
        orr             v2.8b, v16.8b, v18.8b   // test clip == 0 || a0 >= pq
        uqsub           v5.4h, v17.4h, v19.4h   // a0 >= a3 ? a0-a3 : 0  (a0 > a3 in all cases where filtering is enabled, so makes more sense to subtract this way round than the opposite and then taking the abs)
        cmhs            v7.4h, v19.4h, v17.4h   // test a3 >= a0
        mul             v0.4h, v5.4h, v0.h[1]   // a0 >= a3 ? 5*(a0-a3) : 0
        orr             v5.8b, v2.8b, v7.8b     // test clip == 0 || a0 >= pq || a3 >= a0
        mov             w0, v5.s[1]             // move to gp reg
        ushr            v0.4h, v0.4h, #3        // a0 >= a3 ? (5*(a0-a3))>>3 : 0
        cmhs            v5.4h, v0.4h, v4.4h
        tbnz            w0, #0, 1f              // none of the 4 pixel pairs should be updated if this one is not filtered
        bsl             v5.8b, v4.8b, v0.8b     // FFMIN(d, clip)
        bic             v0.8b, v5.8b, v2.8b     // set each d to zero if it should not be filtered because clip == 0 || a0 >= pq (a3 > a0 case already zeroed by saturating sub)
        mls             v6.4h, v0.4h, v3.4h     // invert d depending on clip_sign & a0_sign, or zero it if they match, and accumulate into P4
        mla             v1.4h, v0.4h, v3.4h     // invert d depending on clip_sign & a0_sign, or zero it if they match, and accumulate into P5
        sqxtun          v0.8b, v6.8h
        sqxtun          v1.8b, v1.8h
        st1             {v0.s}[0], [x3], x1
        st1             {v1.s}[0], [x3]
1:      ret
endfunc

// VC-1 in-loop deblocking filter for 4 pixel pairs at boundary of horizontally-neighbouring blocks
// On entry:
//   x0 -> top-left pel of right block
//   x1 = row stride, bytes
//   w2 = PQUANT bitstream parameter
function ff_vc1_h_loop_filter4_neon, export=1
        sub             x3, x0, #4              // where to start reading
        ldr             d0, .Lcoeffs
        ld1             {v1.8b}, [x3], x1
        sub             x0, x0, #1              // where to start writing
        ld1             {v2.8b}, [x3], x1
        ld1             {v3.8b}, [x3], x1
        ld1             {v4.8b}, [x3]
        dup             v5.8h, w2               // pq
        trn1            v6.8b, v1.8b, v2.8b
        trn2            v1.8b, v1.8b, v2.8b
        trn1            v2.8b, v3.8b, v4.8b
        trn2            v3.8b, v3.8b, v4.8b
        trn1            v4.4h, v6.4h, v2.4h     // P1, P5
        trn1            v7.4h, v1.4h, v3.4h     // P2, P6
        trn2            v2.4h, v6.4h, v2.4h     // P3, P7
        trn2            v1.4h, v1.4h, v3.4h     // P4, P8
        ushll           v3.8h, v4.8b, #1        // 2*P1, 2*P5
        uxtl            v6.8h, v7.8b            // P2, P6
        uxtl            v7.8h, v2.8b            // P3, P7
        uxtl            v1.8h, v1.8b            // P4, P8
        mls             v3.8h, v6.8h, v0.h[1]   // 2*P1-5*P2, 2*P5-5*P6
        ushll           v2.8h, v2.8b, #1        // 2*P3, 2*P7
        uxtl            v4.8h, v4.8b            // P1, P5
        mla             v3.8h, v7.8h, v0.h[1]   // 2*P1-5*P2+5*P3, 2*P5-5*P6+5*P7
        mov             d6, v6.d[1]             // P6
        mls             v3.8h, v1.8h, v0.h[0]   // 2*P1-5*P2+5*P3-2*P4, 2*P5-5*P6+5*P7-2*P8
        mov             d4, v4.d[1]             // P5
        mls             v2.4h, v1.4h, v0.h[1]   // 2*P3-5*P4
        mla             v2.4h, v4.4h, v0.h[1]   // 2*P3-5*P4+5*P5
        sub             v7.4h, v1.4h, v4.4h     // P4-P5
        mls             v2.4h, v6.4h, v0.h[0]   // 2*P3-5*P4+5*P5-2*P6
        srshr           v3.8h, v3.8h, #3
        abs             v6.4h, v7.4h
        sshr            v7.4h, v7.4h, #8        // clip_sign
        srshr           v2.4h, v2.4h, #3
        abs             v3.8h, v3.8h            // a1, a2
        sshr            v6.4h, v6.4h, #1        // clip
        mov             d16, v3.d[1]            // a2
        abs             v17.4h, v2.4h           // a0
        cmeq            v18.4h, v6.4h, #0       // test clip == 0
        sshr            v2.4h, v2.4h, #8        // a0_sign
        cmhs            v19.4h, v3.4h, v16.4h   // test a1 >= a2
        cmhs            v5.4h, v17.4h, v5.4h    // test a0 >= pq
        sub             v2.4h, v7.4h, v2.4h     // clip_sign - a0_sign
        bsl             v19.8b, v16.8b, v3.8b   // a3
        orr             v3.8b, v18.8b, v5.8b    // test clip == 0 || a0 >= pq
        uqsub           v5.4h, v17.4h, v19.4h   // a0 >= a3 ? a0-a3 : 0  (a0 > a3 in all cases where filtering is enabled, so makes more sense to subtract this way round than the opposite and then taking the abs)
        cmhs            v7.4h, v19.4h, v17.4h   // test a3 >= a0
        mul             v0.4h, v5.4h, v0.h[1]   // a0 >= a3 ? 5*(a0-a3) : 0
        orr             v5.8b, v3.8b, v7.8b     // test clip == 0 || a0 >= pq || a3 >= a0
        mov             w2, v5.s[1]             // move to gp reg
        ushr            v0.4h, v0.4h, #3        // a0 >= a3 ? (5*(a0-a3))>>3 : 0
        cmhs            v5.4h, v0.4h, v6.4h
        tbnz            w2, #0, 1f              // none of the 4 pixel pairs should be updated if this one is not filtered
        bsl             v5.8b, v6.8b, v0.8b     // FFMIN(d, clip)
        bic             v0.8b, v5.8b, v3.8b     // set each d to zero if it should not be filtered because clip == 0 || a0 >= pq (a3 > a0 case already zeroed by saturating sub)
        mla             v4.4h, v0.4h, v2.4h     // invert d depending on clip_sign & a0_sign, or zero it if they match, and accumulate into P5
        mls             v1.4h, v0.4h, v2.4h     // invert d depending on clip_sign & a0_sign, or zero it if they match, and accumulate into P4
        sqxtun          v3.8b, v4.8h
        sqxtun          v2.8b, v1.8h
        st2             {v2.b, v3.b}[0], [x0], x1
        st2             {v2.b, v3.b}[1], [x0], x1
        st2             {v2.b, v3.b}[2], [x0], x1
        st2             {v2.b, v3.b}[3], [x0]
1:      ret
endfunc

// VC-1 in-loop deblocking filter for 8 pixel pairs at boundary of vertically-neighbouring blocks
// On entry:
//   x0 -> top-left pel of lower block
//   x1 = row stride, bytes
//   w2 = PQUANT bitstream parameter
function ff_vc1_v_loop_filter8_neon, export=1
        sub             x3, x0, w1, sxtw #2
        ldr             d0, .Lcoeffs
        ld1             {v1.8b}, [x0], x1       // P5
        movi            v2.2d, #0x0000ffff00000000
        ld1             {v3.8b}, [x3], x1       // P1
        ld1             {v4.8b}, [x3], x1       // P2
        ld1             {v5.8b}, [x0], x1       // P6
        ld1             {v6.8b}, [x3], x1       // P3
        ld1             {v7.8b}, [x0], x1       // P7
        ushll           v16.8h, v1.8b, #1       // 2*P5
        ushll           v3.8h, v3.8b, #1        // 2*P1
        ld1             {v17.8b}, [x3]          // P4
        uxtl            v4.8h, v4.8b            // P2
        ld1             {v18.8b}, [x0]          // P8
        uxtl            v5.8h, v5.8b            // P6
        dup             v19.8h, w2              // pq
        uxtl            v20.8h, v6.8b           // P3
        mls             v3.8h, v4.8h, v0.h[1]   // 2*P1-5*P2
        uxtl            v4.8h, v7.8b            // P7
        ushll           v6.8h, v6.8b, #1        // 2*P3
        mls             v16.8h, v5.8h, v0.h[1]  // 2*P5-5*P6
        uxtl            v7.8h, v17.8b           // P4
        uxtl            v17.8h, v18.8b          // P8
        mla             v16.8h, v4.8h, v0.h[1]  // 2*P5-5*P6+5*P7
        uxtl            v1.8h, v1.8b            // P5
        mla             v3.8h, v20.8h, v0.h[1]  // 2*P1-5*P2+5*P3
        sub             v4.8h, v7.8h, v1.8h     // P4-P5
        mls             v6.8h, v7.8h, v0.h[1]   // 2*P3-5*P4
        mls             v16.8h, v17.8h, v0.h[0] // 2*P5-5*P6+5*P7-2*P8
        abs             v17.8h, v4.8h
        sshr            v4.8h, v4.8h, #8        // clip_sign
        mls             v3.8h, v7.8h, v0.h[0]   // 2*P1-5*P2+5*P3-2*P4
        sshr            v17.8h, v17.8h, #1      // clip
        mla             v6.8h, v1.8h, v0.h[1]   // 2*P3-5*P4+5*P5
        srshr           v16.8h, v16.8h, #3
        mls             v6.8h, v5.8h, v0.h[0]   // 2*P3-5*P4+5*P5-2*P6
        cmeq            v5.8h, v17.8h, #0       // test clip == 0
        srshr           v3.8h, v3.8h, #3
        abs             v16.8h, v16.8h          // a2
        abs             v3.8h, v3.8h            // a1
        srshr           v6.8h, v6.8h, #3
        cmhs            v18.8h, v3.8h, v16.8h   // test a1 >= a2
        abs             v20.8h, v6.8h           // a0
        sshr            v6.8h, v6.8h, #8        // a0_sign
        bsl             v18.16b, v16.16b, v3.16b // a3
        cmhs            v3.8h, v20.8h, v19.8h   // test a0 >= pq
        sub             v4.8h, v4.8h, v6.8h     // clip_sign - a0_sign
        uqsub           v6.8h, v20.8h, v18.8h   // a0 >= a3 ? a0-a3 : 0  (a0 > a3 in all cases where filtering is enabled, so makes more sense to subtract this way round than the opposite and then taking the abs)
        cmhs            v16.8h, v18.8h, v20.8h  // test a3 >= a0
        orr             v3.16b, v5.16b, v3.16b  // test clip == 0 || a0 >= pq
        mul             v0.8h, v6.8h, v0.h[1]   // a0 >= a3 ? 5*(a0-a3) : 0
        orr             v5.16b, v3.16b, v16.16b // test clip == 0 || a0 >= pq || a3 >= a0
        cmtst           v2.2d, v5.2d, v2.2d     // if 2nd of each group of is not filtered, then none of the others in the group should be either
        mov             w0, v5.s[1]             // move to gp reg
        ushr            v0.8h, v0.8h, #3        // a0 >= a3 ? (5*(a0-a3))>>3 : 0
        mov             w2, v5.s[3]
        orr             v2.16b, v3.16b, v2.16b
        cmhs            v3.8h, v0.8h, v17.8h
        and             w0, w0, w2
        bsl             v3.16b, v17.16b, v0.16b // FFMIN(d, clip)
        tbnz            w0, #0, 1f              // none of the 8 pixel pairs should be updated in this case
        bic             v0.16b, v3.16b, v2.16b  // set each d to zero if it should not be filtered
        mls             v7.8h, v0.8h, v4.8h     // invert d depending on clip_sign & a0_sign, or zero it if they match, and accumulate into P4
        mla             v1.8h, v0.8h, v4.8h     // invert d depending on clip_sign & a0_sign, or zero it if they match, and accumulate into P5
        sqxtun          v0.8b, v7.8h
        sqxtun          v1.8b, v1.8h
        st1             {v0.8b}, [x3], x1
        st1             {v1.8b}, [x3]
1:      ret
endfunc

// VC-1 in-loop deblocking filter for 8 pixel pairs at boundary of horizontally-neighbouring blocks
// On entry:
//   x0 -> top-left pel of right block
//   x1 = row stride, bytes
//   w2 = PQUANT bitstream parameter
function ff_vc1_h_loop_filter8_neon, export=1
        sub             x3, x0, #4              // where to start reading
        ldr             d0, .Lcoeffs
        ld1             {v1.8b}, [x3], x1       // P1[0], P2[0]...
        sub             x0, x0, #1              // where to start writing
        ld1             {v2.8b}, [x3], x1
        add             x4, x0, x1, lsl #2
        ld1             {v3.8b}, [x3], x1
        ld1             {v4.8b}, [x3], x1
        ld1             {v5.8b}, [x3], x1
        ld1             {v6.8b}, [x3], x1
        ld1             {v7.8b}, [x3], x1
        trn1            v16.8b, v1.8b, v2.8b    // P1[0], P1[1], P3[0]...
        ld1             {v17.8b}, [x3]
        trn2            v1.8b, v1.8b, v2.8b     // P2[0], P2[1], P4[0]...
        trn1            v2.8b, v3.8b, v4.8b     // P1[2], P1[3], P3[2]...
        trn2            v3.8b, v3.8b, v4.8b     // P2[2], P2[3], P4[2]...
        dup             v4.8h, w2               // pq
        trn1            v18.8b, v5.8b, v6.8b    // P1[4], P1[5], P3[4]...
        trn2            v5.8b, v5.8b, v6.8b     // P2[4], P2[5], P4[4]...
        trn1            v6.4h, v16.4h, v2.4h    // P1[0], P1[1], P1[2], P1[3], P5[0]...
        trn1            v19.4h, v1.4h, v3.4h    // P2[0], P2[1], P2[2], P2[3], P6[0]...
        trn1            v20.8b, v7.8b, v17.8b   // P1[6], P1[7], P3[6]...
        trn2            v7.8b, v7.8b, v17.8b    // P2[6], P2[7], P4[6]...
        trn2            v2.4h, v16.4h, v2.4h    // P3[0], P3[1], P3[2], P3[3], P7[0]...
        trn2            v1.4h, v1.4h, v3.4h     // P4[0], P4[1], P4[2], P4[3], P8[0]...
        trn1            v3.4h, v18.4h, v20.4h   // P1[4], P1[5], P1[6], P1[7], P5[4]...
        trn1            v16.4h, v5.4h, v7.4h    // P2[4], P2[5], P2[6], P2[7], P6[4]...
        trn2            v17.4h, v18.4h, v20.4h  // P3[4], P3[5], P3[6], P3[7], P7[4]...
        trn2            v5.4h, v5.4h, v7.4h     // P4[4], P4[5], P4[6], P4[7], P8[4]...
        trn1            v7.2s, v6.2s, v3.2s     // P1
        trn1            v18.2s, v19.2s, v16.2s  // P2
        trn2            v3.2s, v6.2s, v3.2s     // P5
        trn2            v6.2s, v19.2s, v16.2s   // P6
        trn1            v16.2s, v2.2s, v17.2s   // P3
        trn2            v2.2s, v2.2s, v17.2s    // P7
        ushll           v7.8h, v7.8b, #1        // 2*P1
        trn1            v17.2s, v1.2s, v5.2s    // P4
        ushll           v19.8h, v3.8b, #1       // 2*P5
        trn2            v1.2s, v1.2s, v5.2s     // P8
        uxtl            v5.8h, v18.8b           // P2
        uxtl            v6.8h, v6.8b            // P6
        uxtl            v18.8h, v16.8b          // P3
        mls             v7.8h, v5.8h, v0.h[1]   // 2*P1-5*P2
        uxtl            v2.8h, v2.8b            // P7
        ushll           v5.8h, v16.8b, #1       // 2*P3
        mls             v19.8h, v6.8h, v0.h[1]  // 2*P5-5*P6
        uxtl            v16.8h, v17.8b          // P4
        uxtl            v1.8h, v1.8b            // P8
        mla             v19.8h, v2.8h, v0.h[1]  // 2*P5-5*P6+5*P7
        uxtl            v2.8h, v3.8b            // P5
        mla             v7.8h, v18.8h, v0.h[1]  // 2*P1-5*P2+5*P3
        sub             v3.8h, v16.8h, v2.8h    // P4-P5
        mls             v5.8h, v16.8h, v0.h[1]  // 2*P3-5*P4
        mls             v19.8h, v1.8h, v0.h[0]  // 2*P5-5*P6+5*P7-2*P8
        abs             v1.8h, v3.8h
        sshr            v3.8h, v3.8h, #8        // clip_sign
        mls             v7.8h, v16.8h, v0.h[0]  // 2*P1-5*P2+5*P3-2*P4
        sshr            v1.8h, v1.8h, #1        // clip
        mla             v5.8h, v2.8h, v0.h[1]   // 2*P3-5*P4+5*P5
        srshr           v17.8h, v19.8h, #3
        mls             v5.8h, v6.8h, v0.h[0]   // 2*P3-5*P4+5*P5-2*P6
        cmeq            v6.8h, v1.8h, #0        // test clip == 0
        srshr           v7.8h, v7.8h, #3
        abs             v17.8h, v17.8h          // a2
        abs             v7.8h, v7.8h            // a1
        srshr           v5.8h, v5.8h, #3
        cmhs            v18.8h, v7.8h, v17.8h   // test a1 >= a2
        abs             v19.8h, v5.8h           // a0
        sshr            v5.8h, v5.8h, #8        // a0_sign
        bsl             v18.16b, v17.16b, v7.16b // a3
        cmhs            v4.8h, v19.8h, v4.8h    // test a0 >= pq
        sub             v3.8h, v3.8h, v5.8h     // clip_sign - a0_sign
        uqsub           v5.8h, v19.8h, v18.8h   // a0 >= a3 ? a0-a3 : 0  (a0 > a3 in all cases where filtering is enabled, so makes more sense to subtract this way round than the opposite and then taking the abs)
        cmhs            v7.8h, v18.8h, v19.8h   // test a3 >= a0
        orr             v4.16b, v6.16b, v4.16b  // test clip == 0 || a0 >= pq
        mul             v0.8h, v5.8h, v0.h[1]   // a0 >= a3 ? 5*(a0-a3) : 0
        orr             v5.16b, v4.16b, v7.16b  // test clip == 0 || a0 >= pq || a3 >= a0
        mov             w2, v5.s[1]             // move to gp reg
        ushr            v0.8h, v0.8h, #3        // a0 >= a3 ? (5*(a0-a3))>>3 : 0
        mov             w3, v5.s[3]
        cmhs            v5.8h, v0.8h, v1.8h
        and             w5, w2, w3
        bsl             v5.16b, v1.16b, v0.16b  // FFMIN(d, clip)
        tbnz            w5, #0, 2f              // none of the 8 pixel pairs should be updated in this case
        bic             v0.16b, v5.16b, v4.16b  // set each d to zero if it should not be filtered because clip == 0 || a0 >= pq (a3 > a0 case already zeroed by saturating sub)
        mla             v2.8h, v0.8h, v3.8h     // invert d depending on clip_sign & a0_sign, or zero it if they match, and accumulate into P5
        mls             v16.8h, v0.8h, v3.8h    // invert d depending on clip_sign & a0_sign, or zero it if they match, and accumulate into P4
        sqxtun          v1.8b, v2.8h
        sqxtun          v0.8b, v16.8h
        tbnz            w2, #0, 1f              // none of the first 4 pixel pairs should be updated if so
        st2             {v0.b, v1.b}[0], [x0], x1
        st2             {v0.b, v1.b}[1], [x0], x1
        st2             {v0.b, v1.b}[2], [x0], x1
        st2             {v0.b, v1.b}[3], [x0]
1:      tbnz            w3, #0, 2f              // none of the second 4 pixel pairs should be updated if so
        st2             {v0.b, v1.b}[4], [x4], x1
        st2             {v0.b, v1.b}[5], [x4], x1
        st2             {v0.b, v1.b}[6], [x4], x1
        st2             {v0.b, v1.b}[7], [x4]
2:      ret
endfunc

// VC-1 in-loop deblocking filter for 16 pixel pairs at boundary of vertically-neighbouring blocks
// On entry:
//   x0 -> top-left pel of lower block
//   x1 = row stride, bytes
//   w2 = PQUANT bitstream parameter
function ff_vc1_v_loop_filter16_neon, export=1
        sub             x3, x0, w1, sxtw #2
        ldr             d0, .Lcoeffs
        ld1             {v1.16b}, [x0], x1      // P5
        movi            v2.2d, #0x0000ffff00000000
        ld1             {v3.16b}, [x3], x1      // P1
        ld1             {v4.16b}, [x3], x1      // P2
        ld1             {v5.16b}, [x0], x1      // P6
        ld1             {v6.16b}, [x3], x1      // P3
        ld1             {v7.16b}, [x0], x1      // P7
        ushll           v16.8h, v1.8b, #1       // 2*P5[0..7]
        ushll           v17.8h, v3.8b, #1       // 2*P1[0..7]
        ld1             {v18.16b}, [x3]         // P4
        uxtl            v19.8h, v4.8b           // P2[0..7]
        ld1             {v20.16b}, [x0]         // P8
        uxtl            v21.8h, v5.8b           // P6[0..7]
        dup             v22.8h, w2              // pq
        ushll2          v3.8h, v3.16b, #1       // 2*P1[8..15]
        mls             v17.8h, v19.8h, v0.h[1] // 2*P1[0..7]-5*P2[0..7]
        ushll2          v19.8h, v1.16b, #1      // 2*P5[8..15]
        uxtl2           v4.8h, v4.16b           // P2[8..15]
        mls             v16.8h, v21.8h, v0.h[1] // 2*P5[0..7]-5*P6[0..7]
        uxtl2           v5.8h, v5.16b           // P6[8..15]
        uxtl            v23.8h, v6.8b           // P3[0..7]
        uxtl            v24.8h, v7.8b           // P7[0..7]
        mls             v3.8h, v4.8h, v0.h[1]   // 2*P1[8..15]-5*P2[8..15]
        ushll           v4.8h, v6.8b, #1        // 2*P3[0..7]
        uxtl            v25.8h, v18.8b          // P4[0..7]
        mls             v19.8h, v5.8h, v0.h[1]  // 2*P5[8..15]-5*P6[8..15]
        uxtl2           v26.8h, v6.16b          // P3[8..15]
        mla             v17.8h, v23.8h, v0.h[1] // 2*P1[0..7]-5*P2[0..7]+5*P3[0..7]
        uxtl2           v7.8h, v7.16b           // P7[8..15]
        ushll2          v6.8h, v6.16b, #1       // 2*P3[8..15]
        mla             v16.8h, v24.8h, v0.h[1] // 2*P5[0..7]-5*P6[0..7]+5*P7[0..7]
        uxtl2           v18.8h, v18.16b         // P4[8..15]
        uxtl            v23.8h, v20.8b          // P8[0..7]
        mls             v4.8h, v25.8h, v0.h[1]  // 2*P3[0..7]-5*P4[0..7]
        uxtl            v24.8h, v1.8b           // P5[0..7]
        uxtl2           v20.8h, v20.16b         // P8[8..15]
        mla             v3.8h, v26.8h, v0.h[1]  // 2*P1[8..15]-5*P2[8..15]+5*P3[8..15]
        uxtl2           v1.8h, v1.16b           // P5[8..15]
        sub             v26.8h, v25.8h, v24.8h  // P4[0..7]-P5[0..7]
        mla             v19.8h, v7.8h, v0.h[1]  // 2*P5[8..15]-5*P6[8..15]+5*P7[8..15]
        sub             v7.8h, v18.8h, v1.8h    // P4[8..15]-P5[8..15]
        mls             v6.8h, v18.8h, v0.h[1]  // 2*P3[8..15]-5*P4[8..15]
        abs             v27.8h, v26.8h
        sshr            v26.8h, v26.8h, #8      // clip_sign[0..7]
        mls             v17.8h, v25.8h, v0.h[0] // 2*P1[0..7]-5*P2[0..7]+5*P3[0..7]-2*P4[0..7]
        abs             v28.8h, v7.8h
        sshr            v27.8h, v27.8h, #1      // clip[0..7]
        mls             v16.8h, v23.8h, v0.h[0] // 2*P5[0..7]-5*P6[0..7]+5*P7[0..7]-2*P8[0..7]
        sshr            v7.8h, v7.8h, #8        // clip_sign[8..15]
        sshr            v23.8h, v28.8h, #1      // clip[8..15]
        mla             v4.8h, v24.8h, v0.h[1]  // 2*P3[0..7]-5*P4[0..7]+5*P5[0..7]
        cmeq            v28.8h, v27.8h, #0      // test clip[0..7] == 0
        srshr           v17.8h, v17.8h, #3
        mls             v3.8h, v18.8h, v0.h[0]  // 2*P1[8..15]-5*P2[8..15]+5*P3[8..15]-2*P4[8..15]
        cmeq            v29.8h, v23.8h, #0      // test clip[8..15] == 0
        srshr           v16.8h, v16.8h, #3
        mls             v19.8h, v20.8h, v0.h[0] // 2*P5[8..15]-5*P6[8..15]+5*P7[8..15]-2*P8[8..15]
        abs             v17.8h, v17.8h          // a1[0..7]
        mla             v6.8h, v1.8h, v0.h[1]   // 2*P3[8..15]-5*P4[8..15]+5*P5[8..15]
        srshr           v3.8h, v3.8h, #3
        mls             v4.8h, v21.8h, v0.h[0]  // 2*P3[0..7]-5*P4[0..7]+5*P5[0..7]-2*P6[0..7]
        abs             v16.8h, v16.8h          // a2[0..7]
        srshr           v19.8h, v19.8h, #3
        mls             v6.8h, v5.8h, v0.h[0]   // 2*P3[8..15]-5*P4[8..15]+5*P5[8..15]-2*P6[8..15]
        cmhs            v5.8h, v17.8h, v16.8h   // test a1[0..7] >= a2[0..7]
        abs             v3.8h, v3.8h            // a1[8..15]
        srshr           v4.8h, v4.8h, #3
        abs             v19.8h, v19.8h          // a2[8..15]
        bsl             v5.16b, v16.16b, v17.16b // a3[0..7]
        srshr           v6.8h, v6.8h, #3
        cmhs            v16.8h, v3.8h, v19.8h   // test a1[8..15] >= a2[8.15]
        abs             v17.8h, v4.8h           // a0[0..7]
        sshr            v4.8h, v4.8h, #8        // a0_sign[0..7]
        bsl             v16.16b, v19.16b, v3.16b // a3[8..15]
        uqsub           v3.8h, v17.8h, v5.8h    // a0[0..7] >= a3[0..7] ? a0[0..7]-a3[0..7] : 0  (a0 > a3 in all cases where filtering is enabled, so makes more sense to subtract this way round than the opposite and then taking the abs)
        abs             v19.8h, v6.8h           // a0[8..15]
        cmhs            v20.8h, v17.8h, v22.8h  // test a0[0..7] >= pq
        cmhs            v5.8h, v5.8h, v17.8h    // test a3[0..7] >= a0[0..7]
        sub             v4.8h, v26.8h, v4.8h    // clip_sign[0..7] - a0_sign[0..7]
        sshr            v6.8h, v6.8h, #8        // a0_sign[8..15]
        mul             v3.8h, v3.8h, v0.h[1]   // a0[0..7] >= a3[0..7] ? 5*(a0[0..7]-a3[0..7]) : 0
        uqsub           v17.8h, v19.8h, v16.8h  // a0[8..15] >= a3[8..15] ? a0[8..15]-a3[8..15] : 0  (a0 > a3 in all cases where filtering is enabled, so makes more sense to subtract this way round than the opposite and then taking the abs)
        orr             v20.16b, v28.16b, v20.16b // test clip[0..7] == 0 || a0[0..7] >= pq
        cmhs            v21.8h, v19.8h, v22.8h  // test a0[8..15] >= pq
        cmhs            v16.8h, v16.8h, v19.8h  // test a3[8..15] >= a0[8..15]
        mul             v0.8h, v17.8h, v0.h[1]  // a0[8..15] >= a3[8..15] ? 5*(a0[8..15]-a3[8..15]) : 0
        sub             v6.8h, v7.8h, v6.8h     // clip_sign[8..15] - a0_sign[8..15]
        orr             v5.16b, v20.16b, v5.16b // test clip[0..7] == 0 || a0[0..7] >= pq || a3[0..7] >= a0[0..7]
        ushr            v3.8h, v3.8h, #3        // a0[0..7] >= a3[0..7] ? (5*(a0[0..7]-a3[0..7]))>>3 : 0
        orr             v7.16b, v29.16b, v21.16b // test clip[8..15] == 0 || a0[8..15] >= pq
        cmtst           v17.2d, v5.2d, v2.2d    // if 2nd of each group of is not filtered, then none of the others in the group should be either
        mov             w0, v5.s[1]             // move to gp reg
        cmhs            v19.8h, v3.8h, v27.8h
        ushr            v0.8h, v0.8h, #3        // a0[8..15] >= a3[8..15] ? (5*(a0[8..15]-a3[8..15]))>>3 : 0
        mov             w2, v5.s[3]
        orr             v5.16b, v7.16b, v16.16b // test clip[8..15] == 0 || a0[8..15] >= pq || a3[8..15] >= a0[8..15]
        orr             v16.16b, v20.16b, v17.16b
        bsl             v19.16b, v27.16b, v3.16b // FFMIN(d[0..7], clip[0..7])
        cmtst           v2.2d, v5.2d, v2.2d
        cmhs            v3.8h, v0.8h, v23.8h
        mov             w4, v5.s[1]
        mov             w5, v5.s[3]
        and             w0, w0, w2
        bic             v5.16b, v19.16b, v16.16b // set each d[0..7] to zero if it should not be filtered because clip[0..7] == 0 || a0[0..7] >= pq (a3 > a0 case already zeroed by saturating sub)
        orr             v2.16b, v7.16b, v2.16b
        bsl             v3.16b, v23.16b, v0.16b // FFMIN(d[8..15], clip[8..15])
        mls             v25.8h, v5.8h, v4.8h    // invert d[0..7] depending on clip_sign[0..7] & a0_sign[0..7], or zero it if they match, and accumulate into P4[0..7]
        and             w2, w4, w5
        bic             v0.16b, v3.16b, v2.16b  // set each d[8..15] to zero if it should not be filtered because clip[8..15] == 0 || a0[8..15] >= pq (a3 > a0 case already zeroed by saturating sub)
        mla             v24.8h, v5.8h, v4.8h    // invert d[0..7] depending on clip_sign[0..7] & a0_sign[0..7], or zero it if they match, and accumulate into P5[0..7]
        and             w0, w0, w2
        mls             v18.8h, v0.8h, v6.8h    // invert d[8..15] depending on clip_sign[8..15] & a0_sign[8..15], or zero it if they match, and accumulate into P4[8..15]
        sqxtun          v2.8b, v25.8h
        tbnz            w0, #0, 1f              // none of the 16 pixel pairs should be updated in this case
        mla             v1.8h, v0.8h, v6.8h     // invert d[8..15] depending on clip_sign[8..15] & a0_sign[8..15], or zero it if they match, and accumulate into P5[8..15]
        sqxtun          v0.8b, v24.8h
        sqxtun2         v2.16b, v18.8h
        sqxtun2         v0.16b, v1.8h
        st1             {v2.16b}, [x3], x1
        st1             {v0.16b}, [x3]
1:      ret
endfunc

// VC-1 in-loop deblocking filter for 16 pixel pairs at boundary of horizontally-neighbouring blocks
// On entry:
//   x0 -> top-left pel of right block
//   x1 = row stride, bytes
//   w2 = PQUANT bitstream parameter
function ff_vc1_h_loop_filter16_neon, export=1
        sub             x3, x0, #4              // where to start reading
        ldr             d0, .Lcoeffs
        ld1             {v1.8b}, [x3], x1       // P1[0], P2[0]...
        sub             x0, x0, #1              // where to start writing
        ld1             {v2.8b}, [x3], x1
        add             x4, x0, x1, lsl #3
        ld1             {v3.8b}, [x3], x1
        add             x5, x0, x1, lsl #2
        ld1             {v4.8b}, [x3], x1
        add             x6, x4, x1, lsl #2
        ld1             {v5.8b}, [x3], x1
        ld1             {v6.8b}, [x3], x1
        ld1             {v7.8b}, [x3], x1
        trn1            v16.8b, v1.8b, v2.8b    // P1[0], P1[1], P3[0]...
        ld1             {v17.8b}, [x3], x1
        trn2            v1.8b, v1.8b, v2.8b     // P2[0], P2[1], P4[0]...
        ld1             {v2.8b}, [x3], x1
        trn1            v18.8b, v3.8b, v4.8b    // P1[2], P1[3], P3[2]...
        ld1             {v19.8b}, [x3], x1
        trn2            v3.8b, v3.8b, v4.8b     // P2[2], P2[3], P4[2]...
        ld1             {v4.8b}, [x3], x1
        trn1            v20.8b, v5.8b, v6.8b    // P1[4], P1[5], P3[4]...
        ld1             {v21.8b}, [x3], x1
        trn2            v5.8b, v5.8b, v6.8b     // P2[4], P2[5], P4[4]...
        ld1             {v6.8b}, [x3], x1
        trn1            v22.8b, v7.8b, v17.8b   // P1[6], P1[7], P3[6]...
        ld1             {v23.8b}, [x3], x1
        trn2            v7.8b, v7.8b, v17.8b    // P2[6], P2[7], P4[6]...
        ld1             {v17.8b}, [x3], x1
        trn1            v24.8b, v2.8b, v19.8b   // P1[8], P1[9], P3[8]...
        ld1             {v25.8b}, [x3]
        trn2            v2.8b, v2.8b, v19.8b    // P2[8], P2[9], P4[8]...
        trn1            v19.4h, v16.4h, v18.4h  // P1[0], P1[1], P1[2], P1[3], P5[0]...
        trn1            v26.8b, v4.8b, v21.8b   // P1[10], P1[11], P3[10]...
        trn2            v4.8b, v4.8b, v21.8b    // P2[10], P2[11], P4[10]...
        trn1            v21.4h, v1.4h, v3.4h    // P2[0], P2[1], P2[2], P2[3], P6[0]...
        trn1            v27.4h, v20.4h, v22.4h  // P1[4], P1[5], P1[6], P1[7], P5[4]...
        trn1            v28.8b, v6.8b, v23.8b   // P1[12], P1[13], P3[12]...
        trn2            v6.8b, v6.8b, v23.8b    // P2[12], P2[13], P4[12]...
        trn1            v23.4h, v5.4h, v7.4h    // P2[4], P2[5], P2[6], P2[7], P6[4]...
        trn1            v29.4h, v24.4h, v26.4h  // P1[8], P1[9], P1[10], P1[11], P5[8]...
        trn1            v30.8b, v17.8b, v25.8b  // P1[14], P1[15], P3[14]...
        trn2            v17.8b, v17.8b, v25.8b  // P2[14], P2[15], P4[14]...
        trn1            v25.4h, v2.4h, v4.4h    // P2[8], P2[9], P2[10], P2[11], P6[8]...
        trn1            v31.2s, v19.2s, v27.2s  // P1[0..7]
        trn2            v19.2s, v19.2s, v27.2s  // P5[0..7]
        trn1            v27.2s, v21.2s, v23.2s  // P2[0..7]
        trn2            v21.2s, v21.2s, v23.2s  // P6[0..7]
        trn1            v23.4h, v28.4h, v30.4h  // P1[12], P1[13], P1[14], P1[15], P5[12]...
        trn2            v16.4h, v16.4h, v18.4h  // P3[0], P3[1], P3[2], P3[3], P7[0]...
        trn1            v18.4h, v6.4h, v17.4h   // P2[12], P2[13], P2[14], P2[15], P6[12]...
        trn2            v20.4h, v20.4h, v22.4h  // P3[4], P3[5], P3[6], P3[7], P7[4]...
        trn2            v22.4h, v24.4h, v26.4h  // P3[8], P3[9], P3[10], P3[11], P7[8]...
        trn1            v24.2s, v29.2s, v23.2s  // P1[8..15]
        trn2            v23.2s, v29.2s, v23.2s  // P5[8..15]
        trn1            v26.2s, v25.2s, v18.2s  // P2[8..15]
        trn2            v18.2s, v25.2s, v18.2s  // P6[8..15]
        trn2            v25.4h, v28.4h, v30.4h  // P3[12], P3[13], P3[14], P3[15], P7[12]...
        trn2            v1.4h, v1.4h, v3.4h     // P4[0], P4[1], P4[2], P4[3], P8[0]...
        trn2            v3.4h, v5.4h, v7.4h     // P4[4], P4[5], P4[6], P4[7], P8[4]...
        trn2            v2.4h, v2.4h, v4.4h     // P4[8], P4[9], P4[10], P4[11], P8[8]...
        trn2            v4.4h, v6.4h, v17.4h    // P4[12], P4[13], P4[14], P4[15], P8[12]...
        ushll           v5.8h, v31.8b, #1       // 2*P1[0..7]
        ushll           v6.8h, v19.8b, #1       // 2*P5[0..7]
        trn1            v7.2s, v16.2s, v20.2s   // P3[0..7]
        uxtl            v17.8h, v27.8b          // P2[0..7]
        trn2            v16.2s, v16.2s, v20.2s  // P7[0..7]
        uxtl            v20.8h, v21.8b          // P6[0..7]
        trn1            v21.2s, v22.2s, v25.2s  // P3[8..15]
        ushll           v24.8h, v24.8b, #1      // 2*P1[8..15]
        trn2            v22.2s, v22.2s, v25.2s  // P7[8..15]
        ushll           v25.8h, v23.8b, #1      // 2*P5[8..15]
        trn1            v27.2s, v1.2s, v3.2s    // P4[0..7]
        uxtl            v26.8h, v26.8b          // P2[8..15]
        mls             v5.8h, v17.8h, v0.h[1]  // 2*P1[0..7]-5*P2[0..7]
        uxtl            v17.8h, v18.8b          // P6[8..15]
        mls             v6.8h, v20.8h, v0.h[1]  // 2*P5[0..7]-5*P6[0..7]
        trn1            v18.2s, v2.2s, v4.2s    // P4[8..15]
        uxtl            v28.8h, v7.8b           // P3[0..7]
        mls             v24.8h, v26.8h, v0.h[1] // 2*P1[8..15]-5*P2[8..15]
        uxtl            v16.8h, v16.8b          // P7[0..7]
        uxtl            v26.8h, v21.8b          // P3[8..15]
        mls             v25.8h, v17.8h, v0.h[1] // 2*P5[8..15]-5*P6[8..15]
        uxtl            v22.8h, v22.8b          // P7[8..15]
        ushll           v7.8h, v7.8b, #1        // 2*P3[0..7]
        uxtl            v27.8h, v27.8b          // P4[0..7]
        trn2            v1.2s, v1.2s, v3.2s     // P8[0..7]
        ushll           v3.8h, v21.8b, #1       // 2*P3[8..15]
        trn2            v2.2s, v2.2s, v4.2s     // P8[8..15]
        uxtl            v4.8h, v18.8b           // P4[8..15]
        mla             v5.8h, v28.8h, v0.h[1]  // 2*P1[0..7]-5*P2[0..7]+5*P3[0..7]
        uxtl            v1.8h, v1.8b            // P8[0..7]
        mla             v6.8h, v16.8h, v0.h[1]  // 2*P5[0..7]-5*P6[0..7]+5*P7[0..7]
        uxtl            v2.8h, v2.8b            // P8[8..15]
        uxtl            v16.8h, v19.8b          // P5[0..7]
        mla             v24.8h, v26.8h, v0.h[1] // 2*P1[8..15]-5*P2[8..15]+5*P3[8..15]
        uxtl            v18.8h, v23.8b          // P5[8..15]
        dup             v19.8h, w2              // pq
        mla             v25.8h, v22.8h, v0.h[1] // 2*P5[8..15]-5*P6[8..15]+5*P7[8..15]
        sub             v21.8h, v27.8h, v16.8h  // P4[0..7]-P5[0..7]
        sub             v22.8h, v4.8h, v18.8h   // P4[8..15]-P5[8..15]
        mls             v7.8h, v27.8h, v0.h[1]  // 2*P3[0..7]-5*P4[0..7]
        abs             v23.8h, v21.8h
        mls             v3.8h, v4.8h, v0.h[1]   // 2*P3[8..15]-5*P4[8..15]
        abs             v26.8h, v22.8h
        sshr            v21.8h, v21.8h, #8      // clip_sign[0..7]
        mls             v5.8h, v27.8h, v0.h[0]  // 2*P1[0..7]-5*P2[0..7]+5*P3[0..7]-2*P4[0..7]
        sshr            v23.8h, v23.8h, #1      // clip[0..7]
        sshr            v26.8h, v26.8h, #1      // clip[8..15]
        mls             v6.8h, v1.8h, v0.h[0]   // 2*P5[0..7]-5*P6[0..7]+5*P7[0..7]-2*P8[0..7]
        sshr            v1.8h, v22.8h, #8       // clip_sign[8..15]
        cmeq            v22.8h, v23.8h, #0      // test clip[0..7] == 0
        mls             v24.8h, v4.8h, v0.h[0]  // 2*P1[8..15]-5*P2[8..15]+5*P3[8..15]-2*P4[8..15]
        cmeq            v28.8h, v26.8h, #0      // test clip[8..15] == 0
        srshr           v5.8h, v5.8h, #3
        mls             v25.8h, v2.8h, v0.h[0]  // 2*P5[8..15]-5*P6[8..15]+5*P7[8..15]-2*P8[8..15]
        srshr           v2.8h, v6.8h, #3
        mla             v7.8h, v16.8h, v0.h[1]  // 2*P3[0..7]-5*P4[0..7]+5*P5[0..7]
        srshr           v6.8h, v24.8h, #3
        mla             v3.8h, v18.8h, v0.h[1]  // 2*P3[8..15]-5*P4[8..15]+5*P5[8..15]
        abs             v5.8h, v5.8h            // a1[0..7]
        srshr           v24.8h, v25.8h, #3
        mls             v3.8h, v17.8h, v0.h[0]  // 2*P3[8..15]-5*P4[8..15]+5*P5[8..15]-2*P6[8..15]
        abs             v2.8h, v2.8h            // a2[0..7]
        abs             v6.8h, v6.8h            // a1[8..15]
        mls             v7.8h, v20.8h, v0.h[0]  // 2*P3[0..7]-5*P4[0..7]+5*P5[0..7]-2*P6[0..7]
        abs             v17.8h, v24.8h          // a2[8..15]
        cmhs            v20.8h, v5.8h, v2.8h    // test a1[0..7] >= a2[0..7]
        srshr           v3.8h, v3.8h, #3
        cmhs            v24.8h, v6.8h, v17.8h   // test a1[8..15] >= a2[8.15]
        srshr           v7.8h, v7.8h, #3
        bsl             v20.16b, v2.16b, v5.16b // a3[0..7]
        abs             v2.8h, v3.8h            // a0[8..15]
        sshr            v3.8h, v3.8h, #8        // a0_sign[8..15]
        bsl             v24.16b, v17.16b, v6.16b // a3[8..15]
        abs             v5.8h, v7.8h            // a0[0..7]
        sshr            v6.8h, v7.8h, #8        // a0_sign[0..7]
        cmhs            v7.8h, v2.8h, v19.8h    // test a0[8..15] >= pq
        sub             v1.8h, v1.8h, v3.8h     // clip_sign[8..15] - a0_sign[8..15]
        uqsub           v3.8h, v2.8h, v24.8h    // a0[8..15] >= a3[8..15] ? a0[8..15]-a3[8..15] : 0  (a0 > a3 in all cases where filtering is enabled, so makes more sense to subtract this way round than the opposite and then taking the abs)
        cmhs            v2.8h, v24.8h, v2.8h    // test a3[8..15] >= a0[8..15]
        uqsub           v17.8h, v5.8h, v20.8h   // a0[0..7] >= a3[0..7] ? a0[0..7]-a3[0..7] : 0  (a0 > a3 in all cases where filtering is enabled, so makes more sense to subtract this way round than the opposite and then taking the abs)
        cmhs            v19.8h, v5.8h, v19.8h   // test a0[0..7] >= pq
        orr             v7.16b, v28.16b, v7.16b // test clip[8..15] == 0 || a0[8..15] >= pq
        sub             v6.8h, v21.8h, v6.8h    // clip_sign[0..7] - a0_sign[0..7]
        mul             v3.8h, v3.8h, v0.h[1]   // a0[8..15] >= a3[8..15] ? 5*(a0[8..15]-a3[8..15]) : 0
        cmhs            v5.8h, v20.8h, v5.8h    // test a3[0..7] >= a0[0..7]
        orr             v19.16b, v22.16b, v19.16b // test clip[0..7] == 0 || a0[0..7] >= pq
        mul             v0.8h, v17.8h, v0.h[1]  // a0[0..7] >= a3[0..7] ? 5*(a0[0..7]-a3[0..7]) : 0
        orr             v2.16b, v7.16b, v2.16b  // test clip[8..15] == 0 || a0[8..15] >= pq || a3[8..15] >= a0[8..15]
        orr             v5.16b, v19.16b, v5.16b // test clip[0..7] == 0 || a0[0..7] >= pq || a3[0..7] >= a0[0..7]
        ushr            v3.8h, v3.8h, #3        // a0[8..15] >= a3[8..15] ? (5*(a0[8..15]-a3[8..15]))>>3 : 0
        mov             w7, v2.s[1]
        mov             w8, v2.s[3]
        ushr            v0.8h, v0.8h, #3        // a0[0..7] >= a3[0..7] ? (5*(a0[0..7]-a3[0..7]))>>3 : 0
        mov             w2, v5.s[1]             // move to gp reg
        cmhs            v2.8h, v3.8h, v26.8h
        mov             w3, v5.s[3]
        cmhs            v5.8h, v0.8h, v23.8h
        bsl             v2.16b, v26.16b, v3.16b // FFMIN(d[8..15], clip[8..15])
        and             w9, w7, w8
        bsl             v5.16b, v23.16b, v0.16b // FFMIN(d[0..7], clip[0..7])
        and             w10, w2, w3
        bic             v0.16b, v2.16b, v7.16b  // set each d[8..15] to zero if it should not be filtered because clip[8..15] == 0 || a0[8..15] >= pq (a3 > a0 case already zeroed by saturating sub)
        and             w9, w10, w9
        bic             v2.16b, v5.16b, v19.16b // set each d[0..7] to zero if it should not be filtered because clip[0..7] == 0 || a0[0..7] >= pq (a3 > a0 case already zeroed by saturating sub)
        mls             v4.8h, v0.8h, v1.8h     // invert d[8..15] depending on clip_sign[8..15] & a0_sign[8..15], or zero it if they match, and accumulate into P4
        tbnz            w9, #0, 4f              // none of the 16 pixel pairs should be updated in this case
        mls             v27.8h, v2.8h, v6.8h    // invert d[0..7] depending on clip_sign[0..7] & a0_sign[0..7], or zero it if they match, and accumulate into P4
        mla             v16.8h, v2.8h, v6.8h    // invert d[0..7] depending on clip_sign[0..7] & a0_sign[0..7], or zero it if they match, and accumulate into P5
        sqxtun          v2.8b, v4.8h
        mla             v18.8h, v0.8h, v1.8h    // invert d[8..15] depending on clip_sign[8..15] & a0_sign[8..15], or zero it if they match, and accumulate into P5
        sqxtun          v0.8b, v27.8h
        sqxtun          v1.8b, v16.8h
        sqxtun          v3.8b, v18.8h
        tbnz            w2, #0, 1f
        st2             {v0.b, v1.b}[0], [x0], x1
        st2             {v0.b, v1.b}[1], [x0], x1
        st2             {v0.b, v1.b}[2], [x0], x1
        st2             {v0.b, v1.b}[3], [x0]
1:      tbnz            w3, #0, 2f
        st2             {v0.b, v1.b}[4], [x5], x1
        st2             {v0.b, v1.b}[5], [x5], x1
        st2             {v0.b, v1.b}[6], [x5], x1
        st2             {v0.b, v1.b}[7], [x5]
2:      tbnz            w7, #0, 3f
        st2             {v2.b, v3.b}[0], [x4], x1
        st2             {v2.b, v3.b}[1], [x4], x1
        st2             {v2.b, v3.b}[2], [x4], x1
        st2             {v2.b, v3.b}[3], [x4]
3:      tbnz            w8, #0, 4f
        st2             {v2.b, v3.b}[4], [x6], x1
        st2             {v2.b, v3.b}[5], [x6], x1
        st2             {v2.b, v3.b}[6], [x6], x1
        st2             {v2.b, v3.b}[7], [x6]
4:      ret
endfunc

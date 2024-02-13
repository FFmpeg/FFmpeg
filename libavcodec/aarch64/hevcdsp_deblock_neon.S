/* -*-arm64-*-
 * vim: syntax=arm64asm
 *
 * Copyright (c) 2014 Seppo Tomperi <seppo.tomperi@vtt.fi>
 * Copyright (c) 2023 J. Dekker <jdek@itanimul.li>
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
#include "neon.S"

.macro hevc_loop_filter_chroma_start bitdepth
        mov             x4, x30
        ldr             w14, [x2]
        ldr             w15, [x2, #4]
.if \bitdepth > 8
        lsl             w14, w14, #(\bitdepth - 8)
        lsl             w15, w15, #(\bitdepth - 8)
.endif
        adds            w2, w14, w15
        b.eq            1f
        dup             v16.4h, w14
        dup             v17.4h, w15
        trn1            v16.2d, v16.2d, v17.2d
.if \bitdepth > 8
        mvni            v19.8h, #((0xff << (\bitdepth - 8)) & 0xff), lsl #8
        movi            v18.8h, #0
.endif
        neg             v17.8h, v16.8h
.endm

.macro hevc_loop_filter_chroma_body bitdepth
.if \bitdepth <= 8
        uxtl            v20.8h, v0.8b // p1
        uxtl            v1.8h,  v1.8b // p0
        uxtl            v2.8h,  v2.8b // q0
        uxtl            v23.8h, v3.8b // q1
        va              .req v20
        vb              .req v23
.else   // required to specify both cases as we are unable to do: v0 .req v20
        va              .req v0
        vb              .req v3
.endif
        sub             v5.8h, v2.8h, v1.8h // q0 - p0
        sub             v6.8h, va.8h, vb.8h // p1 - q1
        shl             v5.8h, v5.8h, #2
        add             v5.8h, v6.8h, v5.8h
        srshr           v5.8h, v5.8h, #3
        clip            v17.8h, v16.8h, v5.8h
        sqadd           v1.8h, v1.8h, v5.8h // p0 + delta
        sqsub           v2.8h, v2.8h, v5.8h // q0 - delta
.if \bitdepth <= 8
        sqxtun          v1.8b, v1.8h
        sqxtun          v2.8b, v2.8h
.else
        clip            v18.8h, v19.8h, v1.8h, v2.8h
.endif
.unreq  va
.unreq  vb
.endm

function hevc_loop_filter_chroma_body_8_neon, export=0
        hevc_loop_filter_chroma_body 8
        ret
endfunc

function hevc_loop_filter_chroma_body_10_neon, export=0
hevc_loop_filter_chroma_body_12_neon:
        hevc_loop_filter_chroma_body 10
        ret
endfunc

// void ff_hevc_h_loop_filter_chroma_8_neon(uint8_t *_pix, ptrdiff_t _stride, int *_tc, uint8_t *_no_p, uint8_t *_no_q);

.macro hevc_h_loop_filter_chroma bitdepth
function ff_hevc_h_loop_filter_chroma_\bitdepth\()_neon, export=1
        hevc_loop_filter_chroma_start \bitdepth
        sub             x0, x0, x1, lsl #1
.if \bitdepth > 8
        ld1             {v0.8h}, [x0], x1
        ld1             {v1.8h}, [x0], x1
        ld1             {v2.8h}, [x0], x1
        ld1             {v3.8h}, [x0]
.else
        ld1             {v0.8b}, [x0], x1
        ld1             {v1.8b}, [x0], x1
        ld1             {v2.8b}, [x0], x1
        ld1             {v3.8b}, [x0]
.endif
        sub             x0, x0, x1, lsl #1
        bl              hevc_loop_filter_chroma_body_\bitdepth\()_neon
.if \bitdepth > 8
        st1             {v1.8h}, [x0], x1
        st1             {v2.8h}, [x0]
.else
        st1             {v1.8b}, [x0], x1
        st1             {v2.8b}, [x0]
.endif
1:      ret             x4
endfunc
.endm

.macro hevc_v_loop_filter_chroma bitdepth
function ff_hevc_v_loop_filter_chroma_\bitdepth\()_neon, export=1
        hevc_loop_filter_chroma_start \bitdepth
.if \bitdepth > 8
        sub             x0, x0, #4
        add             x3, x0, x1
        lsl             x1, x1, #1
        ld1             {v0.d}[0], [x0], x1
        ld1             {v1.d}[0], [x3], x1
        ld1             {v2.d}[0], [x0], x1
        ld1             {v3.d}[0], [x3], x1
        ld1             {v0.d}[1], [x0], x1
        ld1             {v1.d}[1], [x3], x1
        ld1             {v2.d}[1], [x0], x1
        ld1             {v3.d}[1], [x3], x1
        transpose_4x8H  v0, v1, v2, v3, v28, v29, v30, v31
.else
        sub             x0, x0, #2
        add             x3, x0, x1
        lsl             x1, x1, #1
        ld1             {v0.s}[0], [x0], x1
        ld1             {v1.s}[0], [x3], x1
        ld1             {v2.s}[0], [x0], x1
        ld1             {v3.s}[0], [x3], x1
        ld1             {v0.s}[1], [x0], x1
        ld1             {v1.s}[1], [x3], x1
        ld1             {v2.s}[1], [x0], x1
        ld1             {v3.s}[1], [x3], x1
        transpose_4x8B  v0, v1, v2, v3, v28, v29, v30, v31
.endif
        sub             x0, x0, x1, lsl #2
        sub             x3, x3, x1, lsl #2
        bl              hevc_loop_filter_chroma_body_\bitdepth\()_neon
.if \bitdepth > 8
        transpose_4x8H  v0, v1, v2, v3, v28, v29, v30, v31
        st1             {v0.d}[0], [x0], x1
        st1             {v1.d}[0], [x3], x1
        st1             {v2.d}[0], [x0], x1
        st1             {v3.d}[0], [x3], x1
        st1             {v0.d}[1], [x0], x1
        st1             {v1.d}[1], [x3], x1
        st1             {v2.d}[1], [x0], x1
        st1             {v3.d}[1], [x3]
.else
        transpose_4x8B  v0, v1, v2, v3, v28, v29, v30, v31
        st1             {v0.s}[0], [x0], x1
        st1             {v1.s}[0], [x3], x1
        st1             {v2.s}[0], [x0], x1
        st1             {v3.s}[0], [x3], x1
        st1             {v0.s}[1], [x0], x1
        st1             {v1.s}[1], [x3], x1
        st1             {v2.s}[1], [x0], x1
        st1             {v3.s}[1], [x3]
.endif
1:      ret             x4
endfunc
.endm

hevc_h_loop_filter_chroma 8
hevc_h_loop_filter_chroma 10
hevc_h_loop_filter_chroma 12

hevc_v_loop_filter_chroma 8
hevc_v_loop_filter_chroma 10
hevc_v_loop_filter_chroma 12

.macro hevc_loop_filter_luma_body bitdepth
function hevc_loop_filter_luma_body_\bitdepth\()_neon, export=0
.if \bitdepth > 8
        lsl             w2, w2, #(\bitdepth - 8) // beta <<= BIT_DEPTH - 8
.else
        uxtl            v0.8h, v0.8b
        uxtl            v1.8h, v1.8b
        uxtl            v2.8h, v2.8b
        uxtl            v3.8h, v3.8b
        uxtl            v4.8h, v4.8b
        uxtl            v5.8h, v5.8b
        uxtl            v6.8h, v6.8b
        uxtl            v7.8h, v7.8b
.endif
        ldr             w7, [x3] // tc[0]
        ldr             w8, [x3, #4] // tc[1]
        dup             v18.4h, w7
        dup             v19.4h, w8
        trn1            v18.2d, v18.2d, v19.2d
.if \bitdepth > 8
        shl             v18.8h, v18.8h, #(\bitdepth - 8)
.endif
        dup             v27.8h, w2 // beta
        // tc25
        shl             v19.8h, v18.8h, #2 // * 4
        add             v19.8h, v19.8h, v18.8h // (tc * 5)
        srshr           v19.8h, v19.8h, #1 // (tc * 5 + 1) >> 1
        sshr            v17.8h, v27.8h, #2 // beta2

        ////// beta_2 check
        // dp0  = abs(P2  - 2 * P1  + P0)
        add             v22.8h, v3.8h, v1.8h
        shl             v23.8h, v2.8h, #1
        sabd            v30.8h, v22.8h, v23.8h
        // dq0  = abs(Q2  - 2 * Q1  + Q0)
        add             v21.8h, v6.8h, v4.8h
        shl             v26.8h, v5.8h, #1
        sabd            v31.8h, v21.8h, v26.8h
        // d0   = dp0 + dq0
        add             v20.8h, v30.8h, v31.8h
        shl             v25.8h, v20.8h, #1
        // (d0 << 1) < beta_2
        cmgt            v23.8h, v17.8h, v25.8h

        ////// beta check
        // d0 + d3 < beta
        mov             x9, #0xFFFF00000000FFFF
        dup             v24.2d, x9
        and             v25.16b, v24.16b, v20.16b
        addp            v25.8h, v25.8h, v25.8h // 1+0 0+1 1+0 0+1
        addp            v25.4h, v25.4h, v25.4h // 1+0+0+1 1+0+0+1
        cmgt            v25.4h, v27.4h, v25.4h // lower/upper mask in h[0/1]
        mov             w9, v25.s[0]
        cmp             w9, #0
        sxtl            v26.4s, v25.4h
        sxtl            v16.2d, v26.2s // full skip mask
        b.eq            3f // skip both blocks

        // TODO: we can check the full skip mask with the weak/strong mask to
        // potentially skip weak or strong calculation entirely if we only have one

        ////// beta_3 check
        // abs(P3  -  P0) + abs(Q3  -  Q0) < beta_3
        sshr            v17.8h, v17.8h, #1 // beta_3
        sabd            v20.8h, v0.8h, v3.8h
        saba            v20.8h, v7.8h, v4.8h
        cmgt            v21.8h, v17.8h, v20.8h

        and             v23.16b, v23.16b, v21.16b

        ////// tc25 check
        // abs(P0  -  Q0) < tc25
        sabd            v20.8h, v3.8h, v4.8h
        cmgt            v21.8h, v19.8h, v20.8h

        and             v23.16b, v23.16b, v21.16b

        ////// Generate low/high line max from lines 0/3/4/7
        // mask out lines 2/3/5/6
        not             v20.16b, v24.16b // 0x0000FFFFFFFF0000
        orr             v23.16b, v23.16b, v20.16b

        // generate weak/strong mask
        uminp           v23.8h, v23.8h, v23.8h // extend to singles
        sxtl            v23.4s, v23.4h
        uminp           v26.4s, v23.4s, v23.4s // check lines
        // extract to gpr
        ext             v25.16b, v26.16b, v26.16b, #2
        zip1            v17.4s, v26.4s, v26.4s
        mov             w12, v25.s[0]
        mov             w11, #0x0000FFFF
        mov             w13, #0xFFFF0000
        // FFFF FFFF -> strong strong
        // FFFF 0000 -> strong weak
        // 0000 FFFF -> weak   strong
        // 0000 0000 -> weak   weak
        cmp             w12, w13
        b.hi            0f // only strong/strong, skip weak nd_p/nd_q calc

        ////// weak nd_p/nd_q
        // d0+d3
        and             v30.16b, v30.16b, v24.16b // d0 __ __ d3 d4 __ __ d7
        and             v31.16b, v31.16b, v24.16b
        addp            v30.8h, v30.8h, v30.8h // [d0+__ __+d3 d4+__ __+d7] [ ... ]
        addp            v31.8h, v31.8h, v31.8h // [d0+d3 d4+d7]
        addp            v30.4h, v30.4h, v30.4h
        addp            v31.4h, v31.4h, v31.4h

        // ((beta + (beta >> 1)) >> 3)
        sshr            v21.8h, v27.8h, #1
        add             v21.8h, v21.8h, v27.8h
        sshr            v21.8h, v21.8h, #3

        // nd_p = dp0 + dp3 < ((beta + (beta >> 1)) >> 3)
        cmgt            v30.8h, v21.8h, v30.8h
        // nd_q = dq0 + dq3 < ((beta + (beta >> 1)) >> 3)
        cmgt            v31.8h, v21.8h, v31.8h

        sxtl            v30.4s, v30.4h
        sxtl            v31.4s, v31.4h
        sxtl            v28.2d, v30.2s
        sxtl            v29.2d, v31.2s

        cmp             w12, w11
        b.lo            1f // can only be weak weak, skip strong

0:      // STRONG FILTER

        // P0 = p0 + av_clip(((p2 + 2 * p1 + 2 * p0 + 2 * q0 + q1 + 4) >> 3) - p0, -tc3, tc3);
        add             v21.8h, v2.8h, v3.8h   // (p1 + p0
        add             v21.8h, v4.8h, v21.8h  //     + q0)
        shl             v21.8h, v21.8h, #1     //           * 2
        add             v22.8h, v1.8h, v5.8h   //   (p2 + q1)
        add             v21.8h, v22.8h, v21.8h // +
        srshr           v21.8h, v21.8h, #3     //               >> 3
        sub             v21.8h, v21.8h, v3.8h  //                    - p0

        // P1 = p1 + av_clip(((p2 + p1 + p0 + q0 + 2) >> 2) - p1, -tc2, tc2);

        add             v22.8h, v1.8h, v2.8h
        add             v23.8h, v3.8h, v4.8h
        add             v22.8h, v22.8h, v23.8h
        srshr           v22.8h, v22.8h, #2
        sub             v22.8h, v22.8h, v2.8h

        // P2 = p2 + av_clip(((2 * p3 + 3 * p2 + p1 + p0 + q0 + 4) >> 3) - p2, -tc, tc);

        add             v23.8h, v0.8h, v1.8h // p3 + p2
        add             v24.8h, v3.8h, v4.8h // p0 + q0
        shl             v23.8h, v23.8h, #1 // * 2
        add             v23.8h, v23.8h, v24.8h
        add             v24.8h, v1.8h, v2.8h // p2 + p1
        add             v23.8h, v23.8h, v24.8h
        srshr           v23.8h, v23.8h, #3
        sub             v23.8h, v23.8h, v1.8h

        // Q0 = q0 + av_clip(((p1 + 2 * p0 + 2 * q0 + 2 * q1 + q2 + 4) >> 3) - q0, -tc3, tc3);
        add             v24.8h, v3.8h, v4.8h   // (p0 + q0
        add             v24.8h, v5.8h, v24.8h  //     + q1)
        shl             v24.8h, v24.8h, #1     //           * 2
        add             v25.8h, v2.8h, v6.8h   //   (p1 + q2)
        add             v24.8h, v25.8h, v24.8h // +
        srshr           v24.8h, v24.8h, #3     //               >> 3
        sub             v24.8h, v24.8h, v4.8h  //                    - q0

        // Q1 = q1 + av_clip(((p0 + q0 + q1 + q2 + 2) >> 2) - q1, -tc2, tc2);

        add             v25.8h, v6.8h, v5.8h
        add             v26.8h, v3.8h, v4.8h
        add             v25.8h, v25.8h, v26.8h
        srshr           v25.8h, v25.8h, #2
        sub             v25.8h, v25.8h, v5.8h

        // Q2 = q2 + av_clip(((2 * q3 + 3 * q2 + q1 + q0 + p0 + 4) >> 3) - q2, -tc, tc);

        add             v26.8h, v7.8h, v6.8h
        add             v27.8h, v6.8h, v5.8h
        shl             v26.8h, v26.8h, #1
        add             v26.8h, v26.8h, v27.8h
        add             v27.8h, v3.8h, v4.8h
        add             v26.8h, v26.8h, v27.8h
        srshr           v26.8h, v26.8h, #3
        sub             v26.8h, v26.8h, v6.8h

        // this clip should work properly
        shl             v30.8h, v18.8h, #1 // tc2
        neg             v31.8h, v30.8h // -tc2
        clip            v31.8h, v30.8h, v21.8h, v22.8h, v23.8h, v24.8h, v25.8h, v26.8h

        and             v21.16b, v21.16b, v16.16b
        and             v22.16b, v22.16b, v16.16b
        and             v23.16b, v23.16b, v16.16b
        and             v24.16b, v24.16b, v16.16b
        and             v25.16b, v25.16b, v16.16b
        and             v26.16b, v26.16b, v16.16b

        add             v23.8h, v23.8h, v1.8h // careful
        add             v22.8h, v22.8h, v2.8h
        add             v21.8h, v21.8h, v3.8h
        add             v24.8h, v24.8h, v4.8h
        add             v25.8h, v25.8h, v5.8h
        add             v26.8h, v26.8h, v6.8h

        cmp             w12, w13
        b.hi            2f // only strong/strong, skip weak

1:      // WEAK FILTER

        // delta0 = (9 * (q0 - p0) - 3 * (q1 - p1) + 8) >> 4
.if \bitdepth < 12
        sub             v27.8h, v4.8h, v3.8h // q0 - p0
        shl             v30.8h, v27.8h, #3 // * 8
        add             v27.8h, v27.8h, v30.8h // 9 * (q0 - p0)

        sub             v30.8h, v5.8h, v2.8h // q1 - p1
        shl             v31.8h, v30.8h, #1 // * 2

        sub             v27.8h, v27.8h, v31.8h
        sub             v27.8h, v27.8h, v30.8h // - 3 * (q1 - p1)
        srshr           v27.8h, v27.8h, #4
.else
        sub             v19.8h, v4.8h, v3.8h // q0 - p0
        sub             v20.8h, v5.8h, v2.8h // q1 - p1

        sshll           v30.4s, v19.4h, #3 // * 8
        sshll2          v31.4s, v19.8h, #3

        shl             v27.8h, v20.8h, #1

        saddw           v30.4s, v30.4s, v19.4h // 9 * (q0 - p0)
        saddw2          v31.4s, v31.4s, v19.8h

        saddl           v19.4s, v27.4h, v20.4h // 3 * (q1 - p1)
        saddl2          v20.4s, v27.8h, v20.8h

        sub             v19.4s, v30.4s, v19.4s
        sub             v20.4s, v31.4s, v20.4s

        sqrshrn         v27.4h, v19.4s, #4
        sqrshrn2        v27.8h, v20.4s, #4
.endif

        // delta0 10tc check mask
        shl             v30.8h, v18.8h, #1 // * 2
        shl             v31.8h, v18.8h, #3 // * 8
        add             v30.8h, v30.8h, v31.8h // 10 * tc
        abs             v31.8h, v27.8h
        cmgt            v20.8h, v30.8h, v31.8h // abs(delta0) < 10 * tc

        and             v20.16b, v20.16b, v16.16b // combine with full mask

        neg             v31.8h, v18.8h // -tc
        clip            v31.8h, v18.8h, v27.8h // delta0 = av_clip(delta0, -tc, tc)

        // deltap1 = av_clip((((p2 + p0 + 1) >> 1) - p1 + delta0) >> 1, -tc_2, tc_2)
        add             v30.8h, v1.8h, v3.8h
        srshr           v30.8h, v30.8h, #1
        sub             v30.8h, v30.8h, v2.8h
        add             v30.8h, v30.8h, v27.8h
        sshr            v30.8h, v30.8h, #1

        // p3 p2 p1 p0 q0 q1 q2 q3
        // v0 v1 v2 v3 v4 v5 v6 v7

        // deltaq1 = av_clip((((q2 + q0 + 1) >> 1) - q1 - delta0) >> 1, -tc_2, tc_2);
        add             v31.8h, v6.8h, v4.8h
        srshr           v31.8h, v31.8h, #1
        sub             v31.8h, v31.8h, v5.8h
        sub             v31.8h, v31.8h, v27.8h
        sshr            v31.8h, v31.8h, #1

        // apply nd_p nd_q mask to deltap1/deltaq1
        and             v30.16b, v30.16b, v28.16b
        and             v31.16b, v31.16b, v29.16b

        // apply full skip mask to deltap1/deltaq1/delta0
        and             v30.16b, v30.16b, v20.16b
        and             v27.16b, v27.16b, v20.16b
        and             v31.16b, v31.16b, v20.16b

        // clip P1/Q1 to -tc_2, tc_2
        sshr            v18.8h, v18.8h, #1 // tc2
        neg             v28.8h, v18.8h
        clip            v28.8h, v18.8h, v30.8h, v31.8h

        // P0 = av_clip_pixel(p0 + delta0)
        // Q0 = av_clip_pixel(q0 - delta0)
        add             v29.8h, v3.8h, v27.8h // P0
        sub             v27.8h, v4.8h, v27.8h // Q0

        // P1 = av_clip_pixel(p1 + deltap1)
        // Q1 = av_clip_pixel(q1 + deltaq1)
        add             v30.8h, v2.8h, v30.8h // P1
        add             v31.8h, v5.8h, v31.8h // Q1

2:      // MIX WEAK/STRONG

        mov             v19.16b, v1.16b
        mov             v20.16b, v6.16b
        // copy selection mask
        mov             v1.16b, v17.16b
        mov             v2.16b, v17.16b
        mov             v3.16b, v17.16b
        mov             v4.16b, v17.16b
        mov             v5.16b, v17.16b
        mov             v6.16b, v17.16b
        // select
        bsl             v1.16b, v23.16b, v19.16b // P2 strong/orig
        bsl             v2.16b, v22.16b, v30.16b // P1 strong/weak
        bsl             v3.16b, v21.16b, v29.16b // P0 strong/weak
        bsl             v4.16b, v24.16b, v27.16b // Q0 strong/weak
        bsl             v5.16b, v25.16b, v31.16b // Q1 strong/weak
        bsl             v6.16b, v26.16b, v20.16b // Q2 strong/orig
        // NOTE: Q3/P3 are unchanged

.if \bitdepth > 8
        movi            v19.8h, #0
        dup             v20.8h, w14
        clip            v19.8h, v20.8h, v1.8h, v2.8h, v3.8h, v4.8h, v5.8h, v6.8h
.else
        sqxtun          v0.8b, v0.8h
        sqxtun          v1.8b, v1.8h
        sqxtun          v2.8b, v2.8h
        sqxtun          v3.8b, v3.8h
        sqxtun          v4.8b, v4.8h
        sqxtun          v5.8b, v5.8h
        sqxtun          v6.8b, v6.8h
        sqxtun          v7.8b, v7.8h
.endif
        ret
3:      ret             x6
endfunc
.endm

hevc_loop_filter_luma_body 8
hevc_loop_filter_luma_body 10
hevc_loop_filter_luma_body 12

// hevc_v_loop_filter_luma(uint8_t *pix, ptrdiff_t stride, int beta, const int32_t *tc, const uint8_t *no_p, const uint8_t *no_q)

.macro hevc_loop_filter_luma dir, bitdepth
function ff_hevc_\dir\()_loop_filter_luma_\bitdepth\()_neon, export=1
        mov             x6, x30
.ifc \dir, v
.if \bitdepth > 8
        sub             x0, x0, #8
.else
        sub             x0, x0, #4
.endif
.else
        sub             x0, x0, x1, lsl #2 // -4 * xstride
.endif
        mov             x10, x0
.if \bitdepth > 8
        ld1             {v0.8h}, [x0], x1
        ld1             {v1.8h}, [x0], x1
        ld1             {v2.8h}, [x0], x1
        ld1             {v3.8h}, [x0], x1
        ld1             {v4.8h}, [x0], x1
        ld1             {v5.8h}, [x0], x1
        ld1             {v6.8h}, [x0], x1
        ld1             {v7.8h}, [x0]
        mov             w14, #((1 << \bitdepth) - 1)
.ifc \dir, v
        transpose_8x8H  v0, v1, v2, v3, v4, v5, v6, v7, v16, v17
.endif
.else
        ld1             {v0.8b}, [x0], x1
        ld1             {v1.8b}, [x0], x1
        ld1             {v2.8b}, [x0], x1
        ld1             {v3.8b}, [x0], x1
        ld1             {v4.8b}, [x0], x1
        ld1             {v5.8b}, [x0], x1
        ld1             {v6.8b}, [x0], x1
        ld1             {v7.8b}, [x0]
.ifc \dir, v
        transpose_8x8B  v0, v1, v2, v3, v4, v5, v6, v7, v16, v17
.endif
.endif
        bl              hevc_loop_filter_luma_body_\bitdepth\()_neon
.if \bitdepth > 8
.ifc \dir, v
        transpose_8x8H  v0, v1, v2, v3, v4, v5, v6, v7, v16, v17
.endif
        st1             {v0.8h}, [x10], x1
        st1             {v1.8h}, [x10], x1
        st1             {v2.8h}, [x10], x1
        st1             {v3.8h}, [x10], x1
        st1             {v4.8h}, [x10], x1
        st1             {v5.8h}, [x10], x1
        st1             {v6.8h}, [x10], x1
        st1             {v7.8h}, [x10]
.else
.ifc \dir, v
        transpose_8x8B  v0, v1, v2, v3, v4, v5, v6, v7, v16, v17
.endif
        st1             {v0.8b}, [x10], x1
        st1             {v1.8b}, [x10], x1
        st1             {v2.8b}, [x10], x1
        st1             {v3.8b}, [x10], x1
        st1             {v4.8b}, [x10], x1
        st1             {v5.8b}, [x10], x1
        st1             {v6.8b}, [x10], x1
        st1             {v7.8b}, [x10]
.endif
        ret             x6
endfunc
.endm

hevc_loop_filter_luma h, 8
hevc_loop_filter_luma h, 10
hevc_loop_filter_luma h, 12

hevc_loop_filter_luma v, 8
hevc_loop_filter_luma v, 10
hevc_loop_filter_luma v, 12

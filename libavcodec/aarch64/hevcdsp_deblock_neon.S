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

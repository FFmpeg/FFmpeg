/*
 * Copyright (c) 2016 Google Inc.
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


// The main loop filter macro is templated and can produce filters for
// vectors of 8 or 16 bytes. The register mapping throughout the filter
// is close to identical to the arm version (please try to maintain this,
// if either is changed!). When the arm version uses e.g. d20 for the
// input variable p3, the aarch64 version uses v20.8b or v20.16b, depending
// on vector length.
//
// The number of elements in the vector is passed in via the macro parameter
// \sz, which is either .8b or .16b. For simple instructions that doesn't
// lengthen or narrow things, this can easily be templated like this:
//      uabd            v4\sz,  v20\sz, v21\sz
//
// For instructions that lengthen or narrow content, the arm version would
// have used q registers. For these instructions, we have macros that expand
// into either a single e.g. uaddl instruction, or into a uaddl + uaddl2
// pair, depending on the \sz parameter. Wherever the arm version would have
// used a q register, these macros instead take two v registers, i.e. q3
// is mapped to v6+v7. For the case with 8 byte input vectors, such a
// lengthening operation is only stored in v6.8h (what was in q3 in the arm
// case), while the 16 byte input vectors will use v6.8h + v7.8h.
// Such a macro invocation would look like this:
//      uaddl_sz        v8.8h,  v9.8h,  v17, v18, \sz
//
// That is, in the 8 byte input vector case, the second register in these
// register pairs will be unused.
// Unfortunately, this makes the code quite hard to read. For readability,
// see the arm version instead.


.macro add_sz dst1, dst2, in1, in2, in3, in4, sz
        add             \dst1,  \in1,  \in3
.ifc \sz, .16b
        add             \dst2,  \in2,  \in4
.endif
.endm

.macro sub_sz dst1, dst2, in1, in2, in3, in4, sz
        sub             \dst1,  \in1,  \in3
.ifc \sz, .16b
        sub             \dst2,  \in2,  \in4
.endif
.endm

.macro uaddw_sz dst1, dst2, in1, in2, in3, sz
        uaddw           \dst1,  \in1, \in3\().8b
.ifc \sz, .16b
        uaddw2          \dst2,  \in2, \in3\().16b
.endif
.endm

.macro usubw_sz dst1, dst2, in1, in2, in3, sz
        usubw           \dst1,  \in1, \in3\().8b
.ifc \sz, .16b
        usubw2          \dst2,  \in2, \in3\().16b
.endif
.endm

.macro usubl_sz dst1, dst2, in1, in2, sz
        usubl           \dst1,  \in1\().8b,  \in2\().8b
.ifc \sz, .16b
        usubl2          \dst2,  \in1\().16b, \in2\().16b
.endif
.endm

.macro sqxtn_sz dst, in1, in2, sz
        sqxtn           \dst\().8b,  \in1
.ifc \sz, .16b
        sqxtn2          \dst\().16b, \in2
.endif
.endm

.macro sqxtun_sz dst, in1, in2, sz
        sqxtun          \dst\().8b,  \in1
.ifc \sz, .16b
        sqxtun2         \dst\().16b, \in2
.endif
.endm

.macro mul_sz dst1, dst2, in1, in2, in3, in4, sz
        mul             \dst1,  \in1,  \in3
.ifc \sz, .16b
        mul             \dst2,  \in2,  \in4
.endif
.endm

.macro saddw_sz dst1, dst2, in1, in2, in3, sz
        saddw           \dst1,  \in1, \in3\().8b
.ifc \sz, .16b
        saddw2          \dst2,  \in2, \in3\().16b
.endif
.endm

.macro ssubw_sz dst1, dst2, in1, in2, in3, sz
        ssubw           \dst1,  \in1, \in3\().8b
.ifc \sz, .16b
        ssubw2          \dst2,  \in2, \in3\().16b
.endif
.endm

.macro uxtl_sz dst1, dst2, in, sz
        uxtl            \dst1,  \in\().8b
.ifc \sz, .16b
        uxtl2           \dst2,  \in\().16b
.endif
.endm

.macro uaddl_sz dst1, dst2, in1, in2, sz
        uaddl           \dst1,  \in1\().8b,  \in2\().8b
.ifc \sz, .16b
        uaddl2          \dst2,  \in1\().16b, \in2\().16b
.endif
.endm

.macro rshrn_sz dst, in1, in2, shift, sz
        rshrn           \dst\().8b,  \in1, \shift
.ifc \sz, .16b
        rshrn2          \dst\().16b, \in2, \shift
.endif
.endm

.macro ushll_sz dst1, dst2, in, shift, sz
        ushll           \dst1,  \in\().8b,  \shift
.ifc \sz, .16b
        ushll2          \dst2,  \in\().16b, \shift
.endif
.endm

// The input to and output from this macro is in the registers v16-v31,
// and v0-v7 are used as scratch registers.
// p7 = v16 .. p3 = v20, p0 = v23, q0 = v24, q3 = v27, q7 = v31
// Depending on the width of the loop filter, we either use v16-v19
// and v28-v31 as temp registers, or v8-v15.
// When comparing to the arm version, tmpq1 == tmp1 + tmp2,
// tmpq2 == tmp3 + tmp4, etc.
.macro loop_filter wd, sz, mix, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8
.if \mix == 0
        dup             v0\sz,  w2        // E
        dup             v2\sz,  w3        // I
        dup             v3\sz,  w4        // H
.else
        dup             v0.8h,  w2        // E
        dup             v2.8h,  w3        // I
        dup             v3.8h,  w4        // H
        rev16           v1.16b, v0.16b    // E
        rev16           v4.16b, v2.16b    // I
        rev16           v5.16b, v3.16b    // H
        uzp1            v0.16b, v0.16b, v1.16b
        uzp1            v2.16b, v2.16b, v4.16b
        uzp1            v3.16b, v3.16b, v5.16b
.endif

        uabd            v4\sz,  v20\sz, v21\sz        // abs(p3 - p2)
        uabd            v5\sz,  v21\sz, v22\sz        // abs(p2 - p1)
        uabd            v6\sz,  v22\sz, v23\sz        // abs(p1 - p0)
        uabd            v7\sz,  v24\sz, v25\sz        // abs(q0 - q1)
        uabd            \tmp1\sz,  v25\sz, v26\sz     // abs(q1 - q2)
        uabd            \tmp2\sz,  v26\sz, v27\sz     // abs(q2 - q3)
        umax            v4\sz,  v4\sz,  v5\sz
        umax            v5\sz,  v6\sz,  v7\sz
        umax            \tmp1\sz, \tmp1\sz, \tmp2\sz
        uabd            v6\sz,  v23\sz, v24\sz        // abs(p0 - q0)
        umax            v4\sz,  v4\sz,  v5\sz
        uqadd           v6\sz,  v6\sz,  v6\sz         // abs(p0 - q0) * 2
        uabd            v5\sz,  v22\sz, v25\sz        // abs(p1 - q1)
        umax            v4\sz,  v4\sz,  \tmp1\sz      // max(abs(p3 - p2), ..., abs(q2 - q3))
        ushr            v5\sz,  v5\sz,  #1
        cmhs            v4\sz,  v2\sz,  v4\sz         // max(abs()) <= I
        uqadd           v6\sz,  v6\sz,  v5\sz         // abs(p0 - q0) * 2 + abs(p1 - q1) >> 1
        cmhs            v5\sz,  v0\sz,  v6\sz
        and             v4\sz,  v4\sz,  v5\sz         // fm

        // If no pixels need filtering, just exit as soon as possible
        mov             x5,  v4.d[0]
.ifc \sz, .16b
        mov             x6,  v4.d[1]
        adds            x5,  x5,  x6
        b.eq            9f
.else
        cbz             x5,  9f
.endif

.if \wd >= 8
        movi            v0\sz,  #1

        uabd            v6\sz,  v20\sz, v23\sz    // abs(p3 - p0)
        uabd            v2\sz,  v21\sz, v23\sz    // abs(p2 - p0)
        uabd            v1\sz,  v22\sz, v23\sz    // abs(p1 - p0)
        uabd            \tmp1\sz,  v25\sz, v24\sz // abs(q1 - q0)
        uabd            \tmp2\sz,  v26\sz, v24\sz // abs(q2 - q0)
        uabd            \tmp3\sz,  v27\sz, v24\sz // abs(q3 - q0)
        umax            v6\sz,  v6\sz,  v2\sz
        umax            v1\sz,  v1\sz,  \tmp1\sz
        umax            \tmp2\sz,  \tmp2\sz,  \tmp3\sz
.if \wd == 16
        uabd            v7\sz,  v16\sz, v23\sz    // abs(p7 - p0)
        umax            v6\sz,  v6\sz,  v1\sz
        uabd            v2\sz,  v17\sz, v23\sz    // abs(p6 - p0)
        umax            v6\sz,  v6\sz,  \tmp2\sz
        uabd            v1\sz,  v18\sz, v23\sz    // abs(p5 - p0)
        cmhs            v6\sz,  v0\sz,  v6\sz     // flat8in
        uabd            v8\sz,  v19\sz, v23\sz    // abs(p4 - p0)
        and             v6\sz,  v6\sz,  v4\sz     // flat8in && fm
        uabd            v9\sz,  v28\sz, v24\sz    // abs(q4 - q0)
        bic             v4\sz,  v4\sz,  v6\sz     // fm && !flat8in
        uabd            v10\sz, v29\sz, v24\sz    // abs(q5 - q0)
        uabd            v11\sz, v30\sz, v24\sz    // abs(q6 - q0)
        uabd            v12\sz, v31\sz, v24\sz    // abs(q7 - q0)

        umax            v7\sz,  v7\sz,  v2\sz
        umax            v1\sz,  v1\sz,  v8\sz
        umax            v9\sz,  v9\sz,  v10\sz
        umax            v11\sz, v11\sz, v12\sz
        // The rest of the calculation of flat8out is interleaved below
.else
        // The rest of the calculation of flat8in is interleaved below
.endif
.endif

        // Calculate the normal inner loop filter for 2 or 4 pixels
        uabd            v5\sz,  v22\sz, v23\sz // abs(p1 - p0)
.if \wd == 16
        umax            v7\sz,  v7\sz,  v1\sz
        umax            v9\sz,  v9\sz,  v11\sz
.elseif \wd == 8
        umax            v6\sz,  v6\sz,  v1\sz
.endif
        uabd            v1\sz,  v25\sz, v24\sz // abs(q1 - q0)
.if \wd == 16
        umax            v7\sz,  v7\sz,  v9\sz
.elseif \wd == 8
        umax            v6\sz,  v6\sz,  \tmp2\sz
.endif
        usubl_sz        \tmp1\().8h,  \tmp2\().8h,  v22,  v25, \sz // p1 - q1
        umax            v5\sz,  v5\sz,  v1\sz  // max(abs(p1 - p0), abs(q1 - q0))
.if \mix != 0
        mov             v1.d[0], x11
.endif
        usubl_sz        \tmp3\().8h,  \tmp4\().8h,  v24,  v23, \sz // q0 - p0
        movi            \tmp5\().8h,  #3
.if \wd == 8
        cmhs            v6\sz,  v0\sz,  v6\sz  // flat8in
.endif
.if \mix != 0
        sxtl            v1.8h,  v1.8b
.endif
        cmhs            v5\sz,  v3\sz,  v5\sz  // !hev
.if \wd == 8
        // If a 4/8 or 8/4 mix is used, clear the relevant half of v6
.if \mix != 0
        and             v6\sz,  v6\sz,  v1.16b
.endif
        and             v6\sz,  v6\sz,  v4\sz  // flat8in && fm
.endif
        sqxtn_sz        \tmp1,        \tmp1\().8h,  \tmp2\().8h, \sz // av_clip_int8(p1 - q1)
.if \wd == 16
        cmhs            v7\sz,  v0\sz,  v7\sz  // flat8out
.elseif \wd == 8
        bic             v4\sz,  v4\sz,  v6\sz  // fm && !flat8in
.endif
        and             v5\sz,  v5\sz,  v4\sz  // !hev && fm && !flat8in
.if \wd == 16
        and             v7\sz,  v7\sz,  v6\sz  // flat8out && flat8in && fm
.endif

        mul_sz          \tmp3\().8h,  \tmp4\().8h,  \tmp3\().8h, \tmp4\().8h,  \tmp5\().8h,  \tmp5\().8h, \sz // 3 * (q0 - p0)
        bic             \tmp1\sz,  \tmp1\sz,  v5\sz    // if (!hev) av_clip_int8 = 0
        movi            v2\sz,  #4
        saddw_sz        \tmp3\().8h,  \tmp4\().8h,  \tmp3\().8h, \tmp4\().8h,  \tmp1, \sz // 3 * (q0 - p0) [+ av_clip_int8(p1 - q1)]
        movi            v3\sz,  #3
        sqxtn_sz        \tmp1,        \tmp3\().8h,  \tmp4\().8h, \sz       // f
.if \wd == 16
        bic             v6\sz,  v6\sz,  v7\sz  // fm && flat8in && !flat8out
.endif

        sqadd           \tmp3\sz,  \tmp1\sz,  v2\sz // FFMIN(f + 4, 127)
        sqadd           \tmp4\sz,  \tmp1\sz,  v3\sz // FFMIN(f + 3, 127)
        uxtl_sz         v0.8h,  v1.8h,  v23, \sz    // p0
        sshr            \tmp3\sz,  \tmp3\sz,  #3    // f1
        sshr            \tmp4\sz,  \tmp4\sz,  #3    // f2

        uxtl_sz         v2.8h,  v3.8h,  v24, \sz    // q0
        saddw_sz        v0.8h,  v1.8h,  v0.8h,  v1.8h,  \tmp4, \sz // p0 + f2
        ssubw_sz        v2.8h,  v3.8h,  v2.8h,  v3.8h,  \tmp3, \sz // q0 - f1
        sqxtun_sz       v0,  v0.8h,  v1.8h,  \sz    // out p0
        sqxtun_sz       v1,  v2.8h,  v3.8h,  \sz    // out q0
        srshr           \tmp3\sz, \tmp3\sz, #1      // f = (f1 + 1) >> 1
        bit             v23\sz, v0\sz,  v4\sz       // if (fm && !flat8in)
        bit             v24\sz, v1\sz,  v4\sz

        uxtl_sz         v0.8h,  v1.8h,  v22, \sz    // p1
        uxtl_sz         v2.8h,  v3.8h,  v25, \sz    // q1
.if \wd >= 8
        mov             x5,  v6.d[0]
.ifc \sz, .16b
        mov             x6,  v6.d[1]
.endif
.endif
        saddw_sz        v0.8h,  v1.8h,  v0.8h,  v1.8h,  \tmp3, \sz // p1 + f
        ssubw_sz        v2.8h,  v3.8h,  v2.8h,  v3.8h,  \tmp3, \sz // q1 - f
        sqxtun_sz       v0,  v0.8h,  v1.8h, \sz     // out p1
        sqxtun_sz       v2,  v2.8h,  v3.8h, \sz     // out q1
.if \wd >= 8
.ifc \sz, .16b
        adds            x5,  x5,  x6
.endif
.endif
        bit             v22\sz, v0\sz,  v5\sz       // if (!hev && fm && !flat8in)
        bit             v25\sz, v2\sz,  v5\sz

        // If no pixels need flat8in, jump to flat8out
        // (or to a writeout of the inner 4 pixels, for wd=8)
.if \wd >= 8
.ifc \sz, .16b
        b.eq            6f
.else
        cbz             x5,  6f
.endif

        // flat8in
        uaddl_sz        \tmp1\().8h, \tmp2\().8h,  v20, v21, \sz
        uaddl_sz        \tmp3\().8h, \tmp4\().8h,  v22, v25, \sz
        uaddl_sz        \tmp5\().8h, \tmp6\().8h,  v20, v22, \sz
        uaddl_sz        \tmp7\().8h, \tmp8\().8h,  v23, v26, \sz
        add_sz          v0.8h,  v1.8h,  \tmp1\().8h, \tmp2\().8h, \tmp1\().8h, \tmp2\().8h, \sz
        uaddw_sz        v0.8h,  v1.8h,  v0.8h,  v1.8h,  v23, \sz
        uaddw_sz        v0.8h,  v1.8h,  v0.8h,  v1.8h,  v24, \sz
        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  \tmp5\().8h, \tmp6\().8h, \sz
        sub_sz          \tmp3\().8h, \tmp4\().8h,  \tmp3\().8h, \tmp4\().8h,  \tmp1\().8h, \tmp2\().8h, \sz
        sub_sz          \tmp7\().8h, \tmp8\().8h,  \tmp7\().8h, \tmp8\().8h,  \tmp5\().8h, \tmp6\().8h, \sz
        rshrn_sz        v2,  v0.8h,  v1.8h,  #3,  \sz // out p2

        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  \tmp3\().8h, \tmp4\().8h, \sz
        uaddl_sz        \tmp1\().8h, \tmp2\().8h,  v20,  v23, \sz
        uaddl_sz        \tmp3\().8h, \tmp4\().8h,  v24,  v27, \sz
        rshrn_sz        v3,  v0.8h,  v1.8h,  #3,  \sz // out p1

        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  \tmp7\().8h, \tmp8\().8h, \sz
        sub_sz          \tmp3\().8h, \tmp4\().8h,  \tmp3\().8h, \tmp4\().8h,  \tmp1\().8h, \tmp2\().8h, \sz
        uaddl_sz        \tmp5\().8h, \tmp6\().8h,  v21,  v24, \sz
        uaddl_sz        \tmp7\().8h, \tmp8\().8h,  v25,  v27, \sz
        rshrn_sz        v4,  v0.8h,  v1.8h,  #3,  \sz // out p0

        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  \tmp3\().8h, \tmp4\().8h, \sz
        sub_sz          \tmp7\().8h, \tmp8\().8h,  \tmp7\().8h, \tmp8\().8h,  \tmp5\().8h, \tmp6\().8h, \sz
        uaddl_sz        \tmp1\().8h, \tmp2\().8h,  v22,  v25, \sz
        uaddl_sz        \tmp3\().8h, \tmp4\().8h,  v26,  v27, \sz
        rshrn_sz        v5,  v0.8h,  v1.8h,  #3,  \sz // out q0

        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  \tmp7\().8h, \tmp8\().8h, \sz
        sub_sz          \tmp3\().8h, \tmp4\().8h,  \tmp3\().8h, \tmp4\().8h,  \tmp1\().8h, \tmp2\().8h, \sz
        rshrn_sz        \tmp5,  v0.8h,  v1.8h,  #3,  \sz // out q1

        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  \tmp3\().8h, \tmp4\().8h, \sz
        // The output here is written back into the input registers. This doesn't
        // matter for the flat8part below, since we only update those pixels
        // which won't be touched below.
        bit             v21\sz, v2\sz,  v6\sz
        bit             v22\sz, v3\sz,  v6\sz
        bit             v23\sz, v4\sz,  v6\sz
        rshrn_sz        \tmp6,  v0.8h,  v1.8h,  #3,  \sz // out q2
        bit             v24\sz, v5\sz,  v6\sz
        bit             v25\sz, \tmp5\sz,  v6\sz
        bit             v26\sz, \tmp6\sz,  v6\sz
.endif
.if \wd == 16
6:
        orr             v2\sz,  v6\sz,  v7\sz
        mov             x5,  v2.d[0]
.ifc \sz, .16b
        mov             x6,  v2.d[1]
        adds            x5,  x5,  x6
        b.ne            1f
.else
        cbnz            x5,  1f
.endif
        // If no pixels needed flat8in nor flat8out, jump to a
        // writeout of the inner 4 pixels
        ret             x14
1:

        mov             x5,  v7.d[0]
.ifc \sz, .16b
        mov             x6,  v7.d[1]
        adds            x5,  x5,  x6
        b.ne            1f
.else
        cbnz            x5,  1f
.endif
        // If no pixels need flat8out, jump to a writeout of the inner 6 pixels
        ret             x15

1:
        // flat8out
        // This writes all outputs into v2-v17 (skipping v6 and v16).
        // If this part is skipped, the output is read from v21-v26 (which is the input
        // to this section).
        ushll_sz        v0.8h,  v1.8h,  v16,  #3,  \sz           // 8 * v16
        usubw_sz        v0.8h,  v1.8h,  v0.8h,  v1.8h,  v16, \sz // 7 * v16
        uaddw_sz        v0.8h,  v1.8h,  v0.8h,  v1.8h,  v17, \sz
        uaddl_sz        v8.8h,  v9.8h,  v17, v18, \sz
        uaddl_sz        v10.8h, v11.8h, v19, v20, \sz
        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  v8.8h,  v9.8h,  \sz
        uaddl_sz        v8.8h,  v9.8h,  v16, v17, \sz
        uaddl_sz        v12.8h, v13.8h, v21, v22, \sz
        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  v10.8h, v11.8h, \sz
        uaddl_sz        v10.8h, v11.8h, v18, v25, \sz
        uaddl_sz        v14.8h, v15.8h, v23, v24, \sz
        sub_sz          v10.8h, v11.8h, v10.8h, v11.8h, v8.8h,  v9.8h,  \sz
        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  v12.8h, v13.8h, \sz
        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  v14.8h, v15.8h, \sz
        uaddl_sz        v12.8h, v13.8h, v16, v18, \sz
        uaddl_sz        v14.8h, v15.8h, v19, v26, \sz
        rshrn_sz        v2,  v0.8h,  v1.8h,  #4,  \sz

        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  v10.8h, v11.8h, \sz
        uaddl_sz        v8.8h,  v9.8h,  v16, v19, \sz
        uaddl_sz        v10.8h, v11.8h, v20, v27, \sz
        sub_sz          v14.8h, v15.8h, v14.8h, v15.8h, v12.8h, v13.8h, \sz
        bif             v2\sz,  v17\sz, v7\sz
        rshrn_sz        v3,  v0.8h,  v1.8h,  #4,  \sz

        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  v14.8h, v15.8h, \sz
        uaddl_sz        v12.8h, v13.8h, v16, v20, \sz
        uaddl_sz        v14.8h, v15.8h, v21, v28, \sz
        sub_sz          v10.8h, v11.8h, v10.8h, v11.8h, v8.8h,  v9.8h,  \sz
        bif             v3\sz,  v18\sz, v7\sz
        rshrn_sz        v4,  v0.8h,  v1.8h,  #4,  \sz

        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  v10.8h, v11.8h, \sz
        uaddl_sz        v8.8h,  v9.8h,  v16, v21, \sz
        uaddl_sz        v10.8h, v11.8h, v22, v29, \sz
        sub_sz          v14.8h, v15.8h, v14.8h, v15.8h, v12.8h, v13.8h, \sz
        bif             v4\sz,  v19\sz, v7\sz
        rshrn_sz        v5,  v0.8h,  v1.8h,  #4,  \sz

        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  v14.8h, v15.8h, \sz
        uaddl_sz        v12.8h, v13.8h, v16, v22, \sz
        uaddl_sz        v14.8h, v15.8h, v23, v30, \sz
        sub_sz          v10.8h, v11.8h, v10.8h, v11.8h, v8.8h,  v9.8h,  \sz
        bif             v5\sz,  v20\sz, v7\sz
        rshrn_sz        v6,  v0.8h,  v1.8h,  #4,  \sz

        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  v10.8h, v11.8h, \sz
        uaddl_sz        v10.8h, v11.8h, v16, v23, \sz
        sub_sz          v14.8h, v15.8h, v14.8h, v15.8h, v12.8h, v13.8h, \sz
        uaddl_sz        v12.8h, v13.8h, v24, v31, \sz
        bif             v6\sz,  v21\sz, v7\sz
        rshrn_sz        v8,  v0.8h,  v1.8h,  #4,  \sz

        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  v14.8h, v15.8h, \sz
        sub_sz          v10.8h, v11.8h, v12.8h, v13.8h, v10.8h, v11.8h, \sz
        uaddl_sz        v12.8h, v13.8h, v17, v24, \sz
        uaddl_sz        v14.8h, v15.8h, v25, v31, \sz
        bif             v8\sz,  v22\sz, v7\sz
        rshrn_sz        v9,  v0.8h,  v1.8h,  #4,  \sz

        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  v10.8h, v11.8h, \sz
        sub_sz          v14.8h, v15.8h, v14.8h, v15.8h, v12.8h, v13.8h, \sz
        uaddl_sz        v12.8h, v13.8h, v26, v31, \sz
        bif             v9\sz,  v23\sz, v7\sz
        rshrn_sz        v10, v0.8h,  v1.8h,  #4,  \sz

        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  v14.8h, v15.8h, \sz
        uaddl_sz        v14.8h, v15.8h, v18, v25, \sz
        uaddl_sz        v18.8h, v19.8h, v19, v26, \sz
        sub_sz          v12.8h, v13.8h, v12.8h, v13.8h, v14.8h, v15.8h, \sz
        uaddl_sz        v14.8h, v15.8h, v27, v31, \sz
        bif             v10\sz, v24\sz, v7\sz
        rshrn_sz        v11, v0.8h,  v1.8h,  #4,  \sz

        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  v12.8h, v13.8h, \sz
        uaddl_sz        v12.8h, v13.8h, v20, v27, \sz
        sub_sz          v14.8h, v15.8h, v14.8h, v15.8h, v18.8h, v19.8h, \sz
        uaddl_sz        v18.8h, v19.8h, v28, v31, \sz
        bif             v11\sz, v25\sz, v7\sz
        sub_sz          v18.8h, v19.8h, v18.8h, v19.8h, v12.8h, v13.8h, \sz
        rshrn_sz        v12, v0.8h,  v1.8h,  #4,  \sz

        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  v14.8h, v15.8h, \sz
        uaddl_sz        v14.8h, v15.8h, v21, v28, \sz
        uaddl_sz        v20.8h, v21.8h, v29, v31, \sz
        bif             v12\sz, v26\sz, v7\sz
        rshrn_sz        v13, v0.8h,  v1.8h,  #4,  \sz

        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  v18.8h, v19.8h, \sz
        sub_sz          v20.8h, v21.8h, v20.8h, v21.8h, v14.8h, v15.8h, \sz
        uaddl_sz        v18.8h, v19.8h, v22, v29, \sz
        uaddl_sz        v22.8h, v23.8h, v30, v31, \sz
        bif             v13\sz, v27\sz, v7\sz
        rshrn_sz        v14, v0.8h,  v1.8h,  #4,  \sz

        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  v20.8h, v21.8h, \sz
        sub_sz          v22.8h, v23.8h, v22.8h, v23.8h, v18.8h, v19.8h, \sz
        bif             v14\sz, v28\sz, v7\sz
        rshrn_sz        v15, v0.8h,  v1.8h,  #4,  \sz

        add_sz          v0.8h,  v1.8h,  v0.8h,  v1.8h,  v22.8h, v23.8h, \sz
        bif             v15\sz, v29\sz, v7\sz
        rshrn_sz        v17, v0.8h,  v1.8h,  #4,  \sz
        bif             v17\sz, v30\sz, v7\sz
.endif
.endm

// For wd <= 8, we use v16-v19 and v28-v31 for temp registers,
// while we need those for inputs/outputs in wd=16 and use v8-v15
// for temp registers there instead.
function vp9_loop_filter_4
        loop_filter     4,  .8b,  0,    v16, v17, v18, v19, v28, v29, v30, v31
        ret
9:
        ret             x10
endfunc

function vp9_loop_filter_4_16b_mix_44
        loop_filter     4,  .16b, 44,   v16, v17, v18, v19, v28, v29, v30, v31
        ret
9:
        ret             x10
endfunc

function vp9_loop_filter_8
        loop_filter     8,  .8b,  0,    v16, v17, v18, v19, v28, v29, v30, v31
        ret
6:
        ret             x13
9:
        ret             x10
endfunc

function vp9_loop_filter_8_16b_mix
        loop_filter     8,  .16b, 88,   v16, v17, v18, v19, v28, v29, v30, v31
        ret
6:
        ret             x13
9:
        ret             x10
endfunc

function vp9_loop_filter_16
        loop_filter     16, .8b,  0,    v8,  v9,  v10, v11, v12, v13, v14, v15
        ret
9:
        ldp             d8,  d9,  [sp], 0x10
        ldp             d10, d11, [sp], 0x10
        ldp             d12, d13, [sp], 0x10
        ldp             d14, d15, [sp], 0x10
        ret             x10
endfunc

function vp9_loop_filter_16_16b
        loop_filter     16, .16b, 0,    v8,  v9,  v10, v11, v12, v13, v14, v15
        ret
9:
        ldp             d8,  d9,  [sp], 0x10
        ldp             d10, d11, [sp], 0x10
        ldp             d12, d13, [sp], 0x10
        ldp             d14, d15, [sp], 0x10
        ret             x10
endfunc

.macro loop_filter_4
        bl              vp9_loop_filter_4
.endm

.macro loop_filter_4_16b_mix mix
        bl              vp9_loop_filter_4_16b_mix_\mix
.endm

.macro loop_filter_8
        // calculate alternative 'return' targets
        adr             x13, 6f
        bl              vp9_loop_filter_8
.endm

.macro loop_filter_8_16b_mix mix
        // calculate alternative 'return' targets
        adr             x13, 6f
.if \mix == 48
        mov             x11, #0xffffffff00000000
.elseif \mix == 84
        mov             x11, #0x00000000ffffffff
.else
        mov             x11, #0xffffffffffffffff
.endif
        bl              vp9_loop_filter_8_16b_mix
.endm

.macro loop_filter_16
        // calculate alternative 'return' targets
        adr             x14, 7f
        adr             x15, 8f
        bl              vp9_loop_filter_16
.endm

.macro loop_filter_16_16b
        // calculate alternative 'return' targets
        adr             x14, 7f
        adr             x15, 8f
        bl              vp9_loop_filter_16_16b
.endm


// The public functions in this file have got the following signature:
// void loop_filter(uint8_t *dst, ptrdiff_t stride, int mb_lim, int lim, int hev_thr);

function ff_vp9_loop_filter_v_4_8_neon, export=1
        mov             x10, x30
        sub             x9,  x0,  x1, lsl #2
        ld1             {v20.8b}, [x9], x1 // p3
        ld1             {v24.8b}, [x0], x1 // q0
        ld1             {v21.8b}, [x9], x1 // p2
        ld1             {v25.8b}, [x0], x1 // q1
        ld1             {v22.8b}, [x9], x1 // p1
        ld1             {v26.8b}, [x0], x1 // q2
        ld1             {v23.8b}, [x9], x1 // p0
        ld1             {v27.8b}, [x0], x1 // q3
        sub             x0,  x0,  x1, lsl #2
        sub             x9,  x9,  x1, lsl #1

        loop_filter_4

        st1             {v22.8b}, [x9], x1
        st1             {v24.8b}, [x0], x1
        st1             {v23.8b}, [x9], x1
        st1             {v25.8b}, [x0], x1

        ret             x10
endfunc

function ff_vp9_loop_filter_v_44_16_neon, export=1
        mov             x10, x30
        sub             x9,  x0,  x1, lsl #2
        ld1             {v20.16b}, [x9], x1 // p3
        ld1             {v24.16b}, [x0], x1 // q0
        ld1             {v21.16b}, [x9], x1 // p2
        ld1             {v25.16b}, [x0], x1 // q1
        ld1             {v22.16b}, [x9], x1 // p1
        ld1             {v26.16b}, [x0], x1 // q2
        ld1             {v23.16b}, [x9], x1 // p0
        ld1             {v27.16b}, [x0], x1 // q3
        sub             x0,  x0,  x1, lsl #2
        sub             x9,  x9,  x1, lsl #1

        loop_filter_4_16b_mix 44

        st1             {v22.16b}, [x9], x1
        st1             {v24.16b}, [x0], x1
        st1             {v23.16b}, [x9], x1
        st1             {v25.16b}, [x0], x1

        ret             x10
endfunc

function ff_vp9_loop_filter_h_4_8_neon, export=1
        mov             x10, x30
        sub             x9,  x0,  #4
        add             x0,  x9,  x1, lsl #2
        ld1             {v20.8b}, [x9], x1
        ld1             {v24.8b}, [x0], x1
        ld1             {v21.8b}, [x9], x1
        ld1             {v25.8b}, [x0], x1
        ld1             {v22.8b}, [x9], x1
        ld1             {v26.8b}, [x0], x1
        ld1             {v23.8b}, [x9], x1
        ld1             {v27.8b}, [x0], x1

        sub             x9,  x9,  x1, lsl #2
        sub             x0,  x0,  x1, lsl #2
        // Move x0/x9 forward by 2 pixels; we don't need to rewrite the
        // outermost 2 pixels since they aren't changed.
        add             x9,  x9,  #2
        add             x0,  x0,  #2

        transpose_8x8B  v20, v21, v22, v23, v24, v25, v26, v27, v28, v29

        loop_filter_4

        // We only will write the mid 4 pixels back; after the loop filter,
        // these are in v22, v23, v24, v25, ordered as rows (8x4 pixels).
        // We need to transpose them to columns, done with a 4x8 transpose
        // (which in practice is two 4x4 transposes of the two 4x4 halves
        // of the 8x4 pixels; into 4x8 pixels).
        transpose_4x8B  v22, v23, v24, v25, v26, v27, v28, v29
        st1             {v22.s}[0], [x9], x1
        st1             {v22.s}[1], [x0], x1
        st1             {v23.s}[0], [x9], x1
        st1             {v23.s}[1], [x0], x1
        st1             {v24.s}[0], [x9], x1
        st1             {v24.s}[1], [x0], x1
        st1             {v25.s}[0], [x9], x1
        st1             {v25.s}[1], [x0], x1

        ret             x10
endfunc

function ff_vp9_loop_filter_h_44_16_neon, export=1
        mov             x10, x30
        sub             x9,  x0,  #4
        add             x0,  x9,  x1, lsl #3
        ld1             {v20.8b},   [x9], x1
        ld1             {v20.d}[1], [x0], x1
        ld1             {v21.8b},   [x9], x1
        ld1             {v21.d}[1], [x0], x1
        ld1             {v22.8b},   [x9], x1
        ld1             {v22.d}[1], [x0], x1
        ld1             {v23.8b},   [x9], x1
        ld1             {v23.d}[1], [x0], x1
        ld1             {v24.8b},   [x9], x1
        ld1             {v24.d}[1], [x0], x1
        ld1             {v25.8b},   [x9], x1
        ld1             {v25.d}[1], [x0], x1
        ld1             {v26.8b},   [x9], x1
        ld1             {v26.d}[1], [x0], x1
        ld1             {v27.8b},   [x9], x1
        ld1             {v27.d}[1], [x0], x1

        sub             x9,  x9,  x1, lsl #3
        sub             x0,  x0,  x1, lsl #3
        add             x9,  x9,  #2
        add             x0,  x0,  #2

        transpose_8x16B v20, v21, v22, v23, v24, v25, v26, v27, v28, v29

        loop_filter_4_16b_mix 44

        transpose_4x16B v22, v23, v24, v25, v26, v27, v28, v29

        st1             {v22.s}[0], [x9], x1
        st1             {v22.s}[2], [x0], x1
        st1             {v23.s}[0], [x9], x1
        st1             {v23.s}[2], [x0], x1
        st1             {v24.s}[0], [x9], x1
        st1             {v24.s}[2], [x0], x1
        st1             {v25.s}[0], [x9], x1
        st1             {v25.s}[2], [x0], x1
        st1             {v22.s}[1], [x9], x1
        st1             {v22.s}[3], [x0], x1
        st1             {v23.s}[1], [x9], x1
        st1             {v23.s}[3], [x0], x1
        st1             {v24.s}[1], [x9], x1
        st1             {v24.s}[3], [x0], x1
        st1             {v25.s}[1], [x9], x1
        st1             {v25.s}[3], [x0], x1

        ret             x10
endfunc

function ff_vp9_loop_filter_v_8_8_neon, export=1
        mov             x10, x30
        sub             x9,  x0,  x1, lsl #2
        ld1             {v20.8b}, [x9], x1 // p3
        ld1             {v24.8b}, [x0], x1 // q0
        ld1             {v21.8b}, [x9], x1 // p2
        ld1             {v25.8b}, [x0], x1 // q1
        ld1             {v22.8b}, [x9], x1 // p1
        ld1             {v26.8b}, [x0], x1 // q2
        ld1             {v23.8b}, [x9], x1 // p0
        ld1             {v27.8b}, [x0], x1 // q3
        sub             x9,  x9,  x1, lsl #2
        sub             x0,  x0,  x1, lsl #2
        add             x9,  x9,  x1

        loop_filter_8

        st1             {v21.8b}, [x9], x1
        st1             {v24.8b}, [x0], x1
        st1             {v22.8b}, [x9], x1
        st1             {v25.8b}, [x0], x1
        st1             {v23.8b}, [x9], x1
        st1             {v26.8b}, [x0], x1

        ret             x10
6:
        sub             x9,  x0,  x1, lsl #1
        st1             {v22.8b}, [x9], x1
        st1             {v24.8b}, [x0], x1
        st1             {v23.8b}, [x9], x1
        st1             {v25.8b}, [x0], x1
        ret             x10
endfunc

.macro mix_v_16 mix
function ff_vp9_loop_filter_v_\mix\()_16_neon, export=1
        mov             x10, x30
        sub             x9,  x0,  x1, lsl #2
        ld1             {v20.16b}, [x9], x1 // p3
        ld1             {v24.16b}, [x0], x1 // q0
        ld1             {v21.16b}, [x9], x1 // p2
        ld1             {v25.16b}, [x0], x1 // q1
        ld1             {v22.16b}, [x9], x1 // p1
        ld1             {v26.16b}, [x0], x1 // q2
        ld1             {v23.16b}, [x9], x1 // p0
        ld1             {v27.16b}, [x0], x1 // q3
        sub             x9,  x9,  x1, lsl #2
        sub             x0,  x0,  x1, lsl #2
        add             x9,  x9,  x1

        loop_filter_8_16b_mix \mix

        st1             {v21.16b}, [x9], x1
        st1             {v24.16b}, [x0], x1
        st1             {v22.16b}, [x9], x1
        st1             {v25.16b}, [x0], x1
        st1             {v23.16b}, [x9], x1
        st1             {v26.16b}, [x0], x1

        ret             x10
6:
        sub             x9,  x0,  x1, lsl #1
        st1             {v22.16b}, [x9], x1
        st1             {v24.16b}, [x0], x1
        st1             {v23.16b}, [x9], x1
        st1             {v25.16b}, [x0], x1
        ret             x10
endfunc
.endm

mix_v_16 48
mix_v_16 84
mix_v_16 88

function ff_vp9_loop_filter_h_8_8_neon, export=1
        mov             x10, x30
        sub             x9,  x0,  #4
        add             x0,  x9,  x1, lsl #2
        ld1             {v20.8b}, [x9], x1
        ld1             {v24.8b}, [x0], x1
        ld1             {v21.8b}, [x9], x1
        ld1             {v25.8b}, [x0], x1
        ld1             {v22.8b}, [x9], x1
        ld1             {v26.8b}, [x0], x1
        ld1             {v23.8b}, [x9], x1
        ld1             {v27.8b}, [x0], x1

        sub             x9,  x9,  x1, lsl #2
        sub             x0,  x0,  x1, lsl #2

        transpose_8x8B  v20, v21, v22, v23, v24, v25, v26, v27, v28, v29

        loop_filter_8

        // Even though only 6 pixels per row have been changed, we write the
        // full 8 pixel registers.
        transpose_8x8B  v20, v21, v22, v23, v24, v25, v26, v27, v28, v29

        st1             {v20.8b}, [x9], x1
        st1             {v24.8b}, [x0], x1
        st1             {v21.8b}, [x9], x1
        st1             {v25.8b}, [x0], x1
        st1             {v22.8b}, [x9], x1
        st1             {v26.8b}, [x0], x1
        st1             {v23.8b}, [x9], x1
        st1             {v27.8b}, [x0], x1

        ret             x10
6:
        // If we didn't need to do the flat8in part, we use the same writeback
        // as in loop_filter_h_4_8.
        add             x9,  x9,  #2
        add             x0,  x0,  #2
        transpose_4x8B  v22, v23, v24, v25, v26, v27, v28, v29
        st1             {v22.s}[0], [x9], x1
        st1             {v22.s}[1], [x0], x1
        st1             {v23.s}[0], [x9], x1
        st1             {v23.s}[1], [x0], x1
        st1             {v24.s}[0], [x9], x1
        st1             {v24.s}[1], [x0], x1
        st1             {v25.s}[0], [x9], x1
        st1             {v25.s}[1], [x0], x1
        ret             x10
endfunc

.macro mix_h_16 mix
function ff_vp9_loop_filter_h_\mix\()_16_neon, export=1
        mov             x10, x30
        sub             x9,  x0,  #4
        add             x0,  x9,  x1, lsl #3
        ld1             {v20.8b},   [x9], x1
        ld1             {v20.d}[1], [x0], x1
        ld1             {v21.8b},   [x9], x1
        ld1             {v21.d}[1], [x0], x1
        ld1             {v22.8b},   [x9], x1
        ld1             {v22.d}[1], [x0], x1
        ld1             {v23.8b},   [x9], x1
        ld1             {v23.d}[1], [x0], x1
        ld1             {v24.8b},   [x9], x1
        ld1             {v24.d}[1], [x0], x1
        ld1             {v25.8b},   [x9], x1
        ld1             {v25.d}[1], [x0], x1
        ld1             {v26.8b},   [x9], x1
        ld1             {v26.d}[1], [x0], x1
        ld1             {v27.8b},   [x9], x1
        ld1             {v27.d}[1], [x0], x1

        sub             x9,  x9,  x1, lsl #3
        sub             x0,  x0,  x1, lsl #3

        transpose_8x16B v20, v21, v22, v23, v24, v25, v26, v27, v28, v29

        loop_filter_8_16b_mix \mix

        transpose_8x16B v20, v21, v22, v23, v24, v25, v26, v27, v28, v29

        st1             {v20.8b},   [x9], x1
        st1             {v20.d}[1], [x0], x1
        st1             {v21.8b},   [x9], x1
        st1             {v21.d}[1], [x0], x1
        st1             {v22.8b},   [x9], x1
        st1             {v22.d}[1], [x0], x1
        st1             {v23.8b},   [x9], x1
        st1             {v23.d}[1], [x0], x1
        st1             {v24.8b},   [x9], x1
        st1             {v24.d}[1], [x0], x1
        st1             {v25.8b},   [x9], x1
        st1             {v25.d}[1], [x0], x1
        st1             {v26.8b},   [x9], x1
        st1             {v26.d}[1], [x0], x1
        st1             {v27.8b},   [x9], x1
        st1             {v27.d}[1], [x0], x1

        ret             x10
6:
        add             x9,  x9,  #2
        add             x0,  x0,  #2
        transpose_4x16B v22, v23, v24, v25, v26, v27, v28, v29
        st1             {v22.s}[0], [x9], x1
        st1             {v22.s}[2], [x0], x1
        st1             {v23.s}[0], [x9], x1
        st1             {v23.s}[2], [x0], x1
        st1             {v24.s}[0], [x9], x1
        st1             {v24.s}[2], [x0], x1
        st1             {v25.s}[0], [x9], x1
        st1             {v25.s}[2], [x0], x1
        st1             {v22.s}[1], [x9], x1
        st1             {v22.s}[3], [x0], x1
        st1             {v23.s}[1], [x9], x1
        st1             {v23.s}[3], [x0], x1
        st1             {v24.s}[1], [x9], x1
        st1             {v24.s}[3], [x0], x1
        st1             {v25.s}[1], [x9], x1
        st1             {v25.s}[3], [x0], x1
        ret             x10
endfunc
.endm

mix_h_16 48
mix_h_16 84
mix_h_16 88

function ff_vp9_loop_filter_v_16_8_neon, export=1
        mov             x10, x30
        stp             d14, d15, [sp, #-0x10]!
        stp             d12, d13, [sp, #-0x10]!
        stp             d10, d11, [sp, #-0x10]!
        stp             d8,  d9,  [sp, #-0x10]!
        sub             x9,  x0,  x1, lsl #3
        ld1             {v16.8b}, [x9], x1 // p7
        ld1             {v24.8b}, [x0], x1 // q0
        ld1             {v17.8b}, [x9], x1 // p6
        ld1             {v25.8b}, [x0], x1 // q1
        ld1             {v18.8b}, [x9], x1 // p5
        ld1             {v26.8b}, [x0], x1 // q2
        ld1             {v19.8b}, [x9], x1 // p4
        ld1             {v27.8b}, [x0], x1 // q3
        ld1             {v20.8b}, [x9], x1 // p3
        ld1             {v28.8b}, [x0], x1 // q4
        ld1             {v21.8b}, [x9], x1 // p2
        ld1             {v29.8b}, [x0], x1 // q5
        ld1             {v22.8b}, [x9], x1 // p1
        ld1             {v30.8b}, [x0], x1 // q6
        ld1             {v23.8b}, [x9], x1 // p0
        ld1             {v31.8b}, [x0], x1 // q7
        sub             x9,  x9,  x1, lsl #3
        sub             x0,  x0,  x1, lsl #3
        add             x9,  x9,  x1

        loop_filter_16

        // If we did the flat8out part, we get the output in
        // v2-v17 (skipping v7 and v16). x9 points to x0 - 7 * stride,
        // store v2-v9 there, and v10-v17 into x0.
        st1             {v2.8b},  [x9], x1
        st1             {v10.8b}, [x0], x1
        st1             {v3.8b},  [x9], x1
        st1             {v11.8b}, [x0], x1
        st1             {v4.8b},  [x9], x1
        st1             {v12.8b}, [x0], x1
        st1             {v5.8b},  [x9], x1
        st1             {v13.8b}, [x0], x1
        st1             {v6.8b},  [x9], x1
        st1             {v14.8b}, [x0], x1
        st1             {v8.8b},  [x9], x1
        st1             {v15.8b}, [x0], x1
        st1             {v9.8b},  [x9], x1
        st1             {v17.8b}, [x0], x1
9:
        ldp             d8,  d9,  [sp], 0x10
        ldp             d10, d11, [sp], 0x10
        ldp             d12, d13, [sp], 0x10
        ldp             d14, d15, [sp], 0x10
        ret             x10
8:
        add             x9,  x9,  x1, lsl #2
        // If we didn't do the flat8out part, the output is left in the
        // input registers.
        st1             {v21.8b}, [x9], x1
        st1             {v24.8b}, [x0], x1
        st1             {v22.8b}, [x9], x1
        st1             {v25.8b}, [x0], x1
        st1             {v23.8b}, [x9], x1
        st1             {v26.8b}, [x0], x1
        b               9b
7:
        sub             x9,  x0,  x1, lsl #1
        st1             {v22.8b}, [x9], x1
        st1             {v24.8b}, [x0], x1
        st1             {v23.8b}, [x9], x1
        st1             {v25.8b}, [x0], x1
        b               9b
endfunc

function ff_vp9_loop_filter_v_16_16_neon, export=1
        mov             x10, x30
        stp             d14, d15, [sp, #-0x10]!
        stp             d12, d13, [sp, #-0x10]!
        stp             d10, d11, [sp, #-0x10]!
        stp             d8,  d9,  [sp, #-0x10]!
        sub             x9,  x0,  x1, lsl #3
        ld1             {v16.16b}, [x9], x1 // p7
        ld1             {v24.16b}, [x0], x1 // q0
        ld1             {v17.16b}, [x9], x1 // p6
        ld1             {v25.16b}, [x0], x1 // q1
        ld1             {v18.16b}, [x9], x1 // p5
        ld1             {v26.16b}, [x0], x1 // q2
        ld1             {v19.16b}, [x9], x1 // p4
        ld1             {v27.16b}, [x0], x1 // q3
        ld1             {v20.16b}, [x9], x1 // p3
        ld1             {v28.16b}, [x0], x1 // q4
        ld1             {v21.16b}, [x9], x1 // p2
        ld1             {v29.16b}, [x0], x1 // q5
        ld1             {v22.16b}, [x9], x1 // p1
        ld1             {v30.16b}, [x0], x1 // q6
        ld1             {v23.16b}, [x9], x1 // p0
        ld1             {v31.16b}, [x0], x1 // q7
        sub             x9,  x9,  x1, lsl #3
        sub             x0,  x0,  x1, lsl #3
        add             x9,  x9,  x1

        loop_filter_16_16b

        st1             {v2.16b},  [x9], x1
        st1             {v10.16b}, [x0], x1
        st1             {v3.16b},  [x9], x1
        st1             {v11.16b}, [x0], x1
        st1             {v4.16b},  [x9], x1
        st1             {v12.16b}, [x0], x1
        st1             {v5.16b},  [x9], x1
        st1             {v13.16b}, [x0], x1
        st1             {v6.16b},  [x9], x1
        st1             {v14.16b}, [x0], x1
        st1             {v8.16b},  [x9], x1
        st1             {v15.16b}, [x0], x1
        st1             {v9.16b},  [x9], x1
        st1             {v17.16b}, [x0], x1
9:
        ldp             d8,  d9,  [sp], 0x10
        ldp             d10, d11, [sp], 0x10
        ldp             d12, d13, [sp], 0x10
        ldp             d14, d15, [sp], 0x10
        ret             x10
8:
        add             x9,  x9,  x1, lsl #2
        st1             {v21.16b}, [x9], x1
        st1             {v24.16b}, [x0], x1
        st1             {v22.16b}, [x9], x1
        st1             {v25.16b}, [x0], x1
        st1             {v23.16b}, [x9], x1
        st1             {v26.16b}, [x0], x1
        b               9b
7:
        sub             x9,  x0,  x1, lsl #1
        st1             {v22.16b}, [x9], x1
        st1             {v24.16b}, [x0], x1
        st1             {v23.16b}, [x9], x1
        st1             {v25.16b}, [x0], x1
        b               9b
endfunc

function ff_vp9_loop_filter_h_16_8_neon, export=1
        mov             x10, x30
        stp             d14, d15, [sp, #-0x10]!
        stp             d12, d13, [sp, #-0x10]!
        stp             d10, d11, [sp, #-0x10]!
        stp             d8,  d9,  [sp, #-0x10]!
        sub             x9,  x0,  #8
        ld1             {v16.8b}, [x9], x1
        ld1             {v24.8b}, [x0], x1
        ld1             {v17.8b}, [x9], x1
        ld1             {v25.8b}, [x0], x1
        ld1             {v18.8b}, [x9], x1
        ld1             {v26.8b}, [x0], x1
        ld1             {v19.8b}, [x9], x1
        ld1             {v27.8b}, [x0], x1
        ld1             {v20.8b}, [x9], x1
        ld1             {v28.8b}, [x0], x1
        ld1             {v21.8b}, [x9], x1
        ld1             {v29.8b}, [x0], x1
        ld1             {v22.8b}, [x9], x1
        ld1             {v30.8b}, [x0], x1
        ld1             {v23.8b}, [x9], x1
        ld1             {v31.8b}, [x0], x1
        sub             x0,  x0,  x1, lsl #3
        sub             x9,  x9,  x1, lsl #3

        // The 16x8 pixels read above is in two 8x8 blocks; the left
        // half in v16-v23, and the right half in v24-v31. Do two 8x8 transposes
        // of this, to get one column per register.
        transpose_8x8B  v16, v17, v18, v19, v20, v21, v22, v23, v0, v1
        transpose_8x8B  v24, v25, v26, v27, v28, v29, v30, v31, v0, v1

        loop_filter_16

        transpose_8x8B  v16, v2,  v3,  v4,  v5,  v6,  v8,  v9,  v0, v1
        transpose_8x8B  v10, v11, v12, v13, v14, v15, v17, v31, v0, v1

        st1             {v16.8b}, [x9], x1
        st1             {v10.8b}, [x0], x1
        st1             {v2.8b},  [x9], x1
        st1             {v11.8b}, [x0], x1
        st1             {v3.8b},  [x9], x1
        st1             {v12.8b}, [x0], x1
        st1             {v4.8b},  [x9], x1
        st1             {v13.8b}, [x0], x1
        st1             {v5.8b},  [x9], x1
        st1             {v14.8b}, [x0], x1
        st1             {v6.8b},  [x9], x1
        st1             {v15.8b}, [x0], x1
        st1             {v8.8b},  [x9], x1
        st1             {v17.8b}, [x0], x1
        st1             {v9.8b},  [x9], x1
        st1             {v31.8b}, [x0], x1
9:
        ldp             d8,  d9,  [sp], 0x10
        ldp             d10, d11, [sp], 0x10
        ldp             d12, d13, [sp], 0x10
        ldp             d14, d15, [sp], 0x10
        ret             x10
8:
        // The same writeback as in loop_filter_h_8_8
        sub             x9,  x0,  #4
        add             x0,  x9,  x1, lsl #2
        transpose_8x8B  v20, v21, v22, v23, v24, v25, v26, v27, v28, v29

        st1             {v20.8b}, [x9], x1
        st1             {v24.8b}, [x0], x1
        st1             {v21.8b}, [x9], x1
        st1             {v25.8b}, [x0], x1
        st1             {v22.8b}, [x9], x1
        st1             {v26.8b}, [x0], x1
        st1             {v23.8b}, [x9], x1
        st1             {v27.8b}, [x0], x1
        b               9b
7:
        // The same writeback as in loop_filter_h_4_8
        sub             x9,  x0,  #2
        add             x0,  x9,  x1, lsl #2
        transpose_4x8B  v22, v23, v24, v25, v26, v27, v28, v29
        st1             {v22.s}[0], [x9], x1
        st1             {v22.s}[1], [x0], x1
        st1             {v23.s}[0], [x9], x1
        st1             {v23.s}[1], [x0], x1
        st1             {v24.s}[0], [x9], x1
        st1             {v24.s}[1], [x0], x1
        st1             {v25.s}[0], [x9], x1
        st1             {v25.s}[1], [x0], x1
        b               9b
endfunc

function ff_vp9_loop_filter_h_16_16_neon, export=1
        mov             x10, x30
        stp             d14, d15, [sp, #-0x10]!
        stp             d12, d13, [sp, #-0x10]!
        stp             d10, d11, [sp, #-0x10]!
        stp             d8,  d9,  [sp, #-0x10]!
        sub             x9,  x0,  #8
        ld1             {v16.8b},   [x9], x1
        ld1             {v24.8b},   [x0], x1
        ld1             {v17.8b},   [x9], x1
        ld1             {v25.8b},   [x0], x1
        ld1             {v18.8b},   [x9], x1
        ld1             {v26.8b},   [x0], x1
        ld1             {v19.8b},   [x9], x1
        ld1             {v27.8b},   [x0], x1
        ld1             {v20.8b},   [x9], x1
        ld1             {v28.8b},   [x0], x1
        ld1             {v21.8b},   [x9], x1
        ld1             {v29.8b},   [x0], x1
        ld1             {v22.8b},   [x9], x1
        ld1             {v30.8b},   [x0], x1
        ld1             {v23.8b},   [x9], x1
        ld1             {v31.8b},   [x0], x1
        ld1             {v16.d}[1], [x9], x1
        ld1             {v24.d}[1], [x0], x1
        ld1             {v17.d}[1], [x9], x1
        ld1             {v25.d}[1], [x0], x1
        ld1             {v18.d}[1], [x9], x1
        ld1             {v26.d}[1], [x0], x1
        ld1             {v19.d}[1], [x9], x1
        ld1             {v27.d}[1], [x0], x1
        ld1             {v20.d}[1], [x9], x1
        ld1             {v28.d}[1], [x0], x1
        ld1             {v21.d}[1], [x9], x1
        ld1             {v29.d}[1], [x0], x1
        ld1             {v22.d}[1], [x9], x1
        ld1             {v30.d}[1], [x0], x1
        ld1             {v23.d}[1], [x9], x1
        ld1             {v31.d}[1], [x0], x1
        sub             x0,  x0,  x1, lsl #4
        sub             x9,  x9,  x1, lsl #4

        transpose_8x16B v16, v17, v18, v19, v20, v21, v22, v23, v0, v1
        transpose_8x16B v24, v25, v26, v27, v28, v29, v30, v31, v0, v1

        loop_filter_16_16b

        transpose_8x16B v16, v2,  v3,  v4,  v5,  v6,  v8,  v9,  v0, v1
        transpose_8x16B v10, v11, v12, v13, v14, v15, v17, v31, v0, v1

        st1             {v16.8b},   [x9], x1
        st1             {v10.8b},   [x0], x1
        st1             {v2.8b},    [x9], x1
        st1             {v11.8b},   [x0], x1
        st1             {v3.8b},    [x9], x1
        st1             {v12.8b},   [x0], x1
        st1             {v4.8b},    [x9], x1
        st1             {v13.8b},   [x0], x1
        st1             {v5.8b},    [x9], x1
        st1             {v14.8b},   [x0], x1
        st1             {v6.8b},    [x9], x1
        st1             {v15.8b},   [x0], x1
        st1             {v8.8b},    [x9], x1
        st1             {v17.8b},   [x0], x1
        st1             {v9.8b},    [x9], x1
        st1             {v31.8b},   [x0], x1
        st1             {v16.d}[1], [x9], x1
        st1             {v10.d}[1], [x0], x1
        st1             {v2.d}[1],  [x9], x1
        st1             {v11.d}[1], [x0], x1
        st1             {v3.d}[1],  [x9], x1
        st1             {v12.d}[1], [x0], x1
        st1             {v4.d}[1],  [x9], x1
        st1             {v13.d}[1], [x0], x1
        st1             {v5.d}[1],  [x9], x1
        st1             {v14.d}[1], [x0], x1
        st1             {v6.d}[1],  [x9], x1
        st1             {v15.d}[1], [x0], x1
        st1             {v8.d}[1],  [x9], x1
        st1             {v17.d}[1], [x0], x1
        st1             {v9.d}[1],  [x9], x1
        st1             {v31.d}[1], [x0], x1
9:
        ldp             d8,  d9,  [sp], 0x10
        ldp             d10, d11, [sp], 0x10
        ldp             d12, d13, [sp], 0x10
        ldp             d14, d15, [sp], 0x10
        ret             x10
8:
        sub             x9,  x0,  #4
        add             x0,  x9,  x1, lsl #3
        transpose_8x16B v20, v21, v22, v23, v24, v25, v26, v27, v28, v29

        st1             {v20.8b},   [x9], x1
        st1             {v20.d}[1], [x0], x1
        st1             {v21.8b},   [x9], x1
        st1             {v21.d}[1], [x0], x1
        st1             {v22.8b},   [x9], x1
        st1             {v22.d}[1], [x0], x1
        st1             {v23.8b},   [x9], x1
        st1             {v23.d}[1], [x0], x1
        st1             {v24.8b},   [x9], x1
        st1             {v24.d}[1], [x0], x1
        st1             {v25.8b},   [x9], x1
        st1             {v25.d}[1], [x0], x1
        st1             {v26.8b},   [x9], x1
        st1             {v26.d}[1], [x0], x1
        st1             {v27.8b},   [x9], x1
        st1             {v27.d}[1], [x0], x1
        b               9b
7:
        sub             x9,  x0,  #2
        add             x0,  x9,  x1, lsl #3
        transpose_4x16B v22, v23, v24, v25, v26, v27, v28, v29
        st1             {v22.s}[0], [x9], x1
        st1             {v22.s}[2], [x0], x1
        st1             {v23.s}[0], [x9], x1
        st1             {v23.s}[2], [x0], x1
        st1             {v24.s}[0], [x9], x1
        st1             {v24.s}[2], [x0], x1
        st1             {v25.s}[0], [x9], x1
        st1             {v25.s}[2], [x0], x1
        st1             {v22.s}[1], [x9], x1
        st1             {v22.s}[3], [x0], x1
        st1             {v23.s}[1], [x9], x1
        st1             {v23.s}[3], [x0], x1
        st1             {v24.s}[1], [x9], x1
        st1             {v24.s}[3], [x0], x1
        st1             {v25.s}[1], [x9], x1
        st1             {v25.s}[3], [x0], x1
        b               9b
endfunc

/*
 * Copyright (c) 2015 Junhai ZHANG <243186085@qq.com>
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

.macro  transpose_16b_8x8   r0, r1, r2, r3, r4, r5, r6, r7, r8, r9
        trn1        \r8\().8H,  \r0\().8H,  \r1\().8H
        trn2        \r9\().8H,  \r0\().8H,  \r1\().8H
        trn1        \r1\().8H,  \r2\().8H,  \r3\().8H
        trn2        \r3\().8H,  \r2\().8H,  \r3\().8H
        trn1        \r0\().8H,  \r4\().8H,  \r5\().8H
        trn2        \r5\().8H,  \r4\().8H,  \r5\().8H
        trn1        \r2\().8H,  \r6\().8H,  \r7\().8H
        trn2        \r7\().8H,  \r6\().8H,  \r7\().8H
        trn1        \r4\().4S,  \r0\().4S,  \r2\().4S
        trn2        \r2\().4S,  \r0\().4S,  \r2\().4S
        trn1        \r6\().4S,  \r5\().4S,  \r7\().4S
        trn2        \r7\().4S,  \r5\().4S,  \r7\().4S
        trn1        \r5\().4S,  \r9\().4S,  \r3\().4S
        trn2        \r9\().4S,  \r9\().4S,  \r3\().4S
        trn1        \r3\().4S,  \r8\().4S,  \r1\().4S
        trn2        \r8\().4S,  \r8\().4S,  \r1\().4S
        trn1        \r0\().2D,  \r3\().2D,  \r4\().2D
        trn2        \r4\().2D,  \r3\().2D,  \r4\().2D
        trn1        \r1\().2D,  \r5\().2D,  \r6\().2D
        trn2        \r5\().2D,  \r5\().2D,  \r6\().2D
        trn2        \r6\().2D,  \r8\().2D,  \r2\().2D
        trn1        \r2\().2D,  \r8\().2D,  \r2\().2D
        trn1        \r3\().2D,  \r9\().2D,  \r7\().2D
        trn2        \r7\().2D,  \r9\().2D,  \r7\().2D
.endm

.macro  transpose_16b_4x4  r0, r1, r2, r3, r4, r5, r6, r7
        trn1        \r4\().4H,  \r0\().4H,  \r1\().4H
        trn2        \r5\().4H,  \r0\().4H,  \r1\().4H
        trn1        \r7\().4H,  \r2\().4H,  \r3\().4H
        trn2        \r6\().4H,  \r2\().4H,  \r3\().4H
        trn1        \r0\().2S,  \r4\().2S,  \r7\().2S
        trn2        \r2\().2S,  \r4\().2S,  \r7\().2S
        trn1        \r1\().2S,  \r5\().2S,  \r6\().2S
        trn2        \r3\().2S,  \r5\().2S,  \r6\().2S
.endm

.macro tr4_luma_shift r0, r1, shift
        saddl       v5.4S, \r0\().4H, \r1\().4H	
        trn2        v1.2D, \r1\().2D, \r1\().2D
        saddl       v2.4S, \r1\().4H, v1.4H
        ssubl       v4.4S, \r0\().4H, v1.4H
        smull2      v6.4S, \r0\().8H, v0.H[0]
        saddl       v7.4S, \r0\().4H, v1.4H
        ssubw       v7.4S, v7.4S, \r1\().4H
        mul         v7.4S, v7.4S, v0.S[0]
        mul         v8.4S, v5.4S, v0.S[1]
        mul         v9.4S, v2.4S, v0.S[2]
        add         v8.4S, v8.4S, v9.4S
        add         v8.4S, v8.4S, v6.4S
        mul         v2.4S, v2.4S, v0.S[1]
        mul         v9.4S, v4.4S, v0.S[2]
        sub         v9.4S, v9.4S, v2.4S
        add         v9.4S, v9.4S, v6.4S
        mul         v5.4S, v5.4S, v0.S[2]
        mul         v4.4S, v4.4S, v0.S[1]
        add         v5.4S, v5.4S, v4.4S
        sub         v5.4S, v5.4S, v6.4S
        sqrshrn     \r0\().4H, v8.4S, \shift
        sqrshrn     \r1\().4H, v9.4S, \shift
        sqrshrn2    \r0\().8H, v7.4S, \shift
        sqrshrn2    \r1\().8H, v5.4S, \shift
.endm

.macro tr4 r0, r1, r2, r3
        smull       v4.4S, \r1\().4H, v0.H[0]
        smull       v6.4S, \r1\().4H, v0.H[1]
        sshll       v2.4S, \r0\().4H, #6
        sshll       v3.4S, \r2\().4H, #6
        add         v5.4S, v2.4S, v3.4S
        sub         v2.4S, v2.4S, v3.4S
        smlal       v4.4S, \r3\().4H, v0.H[1]
        smlsl       v6.4S, \r3\().4H, v0.H[0]
        sub         v3.4S, v5.4S, v4.4S
        add         v4.4S, v5.4S, v4.4S
        add         v5.4S, v2.4S, v6.4S
        sub         v6.4S, v2.4S, v6.4S
.endm

.macro tr4_shift r0, r1, shift
        smull2      v4.4S, \r0\().8H, v0.H[0]
        smull2      v6.4S, \r0\().8H, v0.H[1]
        sshll       v2.4S, \r0\().4H, #6
        sshll       v3.4S, \r1\().4H, #6
        add         v5.4S, v2.4S, v3.4S
        sub         v2.4S, v2.4S, v3.4S
        smlal2      v4.4S, \r1\().8H, v0.H[1]
        smlsl2      v6.4S, \r1\().8H, v0.H[0]
        sub         v3.4S, v5.4S, v4.4S
        add         v4.4S, v5.4S, v4.4S
        add         v5.4S, v2.4S, v6.4S
        sub         v6.4S, v2.4S, v6.4S
        sqrshrn     \r0\().4H, v4.4S, \shift
        sqrshrn     \r1\().4H, v5.4S, \shift
        sqrshrn2    \r0\().8H, v6.4S, \shift
        sqrshrn2    \r1\().8H, v3.4S, \shift
.endm

.macro tr8_begin in0, in1, in2, in3
        smull       v7.4S, \in0\().4H, v0.H[5]
        smull       v8.4S, \in0\().4H, v0.H[4]
        smull       v9.4S, \in0\().4H, v0.H[7]
        smull       v10.4S, \in0\().4H, v0.H[6]
        smlal       v7.4S, \in1\().4H, v0.H[4]
        smlsl       v8.4S, \in1\().4H, v0.H[6]
        smlsl       v9.4S, \in1\().4H, v0.H[5]
        smlsl       v10.4S, \in1\().4H, v0.H[7]
        smlal       v7.4S, \in2\().4H, v0.H[7]
        smlsl       v8.4S, \in2\().4H, v0.H[5]
        smlal       v9.4S, \in2\().4H, v0.H[6]
        smlal       v10.4S, \in2\().4H, v0.H[4]
        smlal       v7.4S, \in3\().4H, v0.H[6]
        smlsl       v8.4S, \in3\().4H, v0.H[7]
        smlal       v9.4S, \in3\().4H, v0.H[4]
        smlsl       v10.4S, \in3\().4H, v0.H[5]
.endm

.macro tr8_end shift
        add         v1.4S, v4.4S, v7.4S
        sub         v4.4S, v4.4S, v7.4S
        add         v2.4S, v5.4S, v8.4S
        sub         v5.4S, v5.4S, v8.4S
        add         v11.4S, v6.4S, v9.4S 
        sub         v6.4S, v6.4S, v9.4S
        add         v12.4S, v3.4S, v10.4S
        sub         v3.4S, v3.4S, v10.4S
        sqrshrn     v9.4H, v4.4S, \shift
        sqrshrn     v8.4H, v5.4S, \shift
        sqrshrn     v7.4H, v6.4S, \shift
        sqrshrn     v6.4H, v3.4S, \shift
        sqrshrn     v5.4H, v12.4S, \shift
        sqrshrn     v4.4H, v11.4S, \shift
        sqrshrn     v3.4H, v2.4S, \shift
        sqrshrn     v2.4H, v1.4S, \shift
.endm

.macro tr8_end_0
        sub         v15.4S, v4.4S, v7.4S
        sub         v14.4S, v5.4S, v8.4S
        sub         v13.4S, v6.4S, v9.4S
        sub         v12.4S, v3.4S, v10.4S
        add         v11.4S, v3.4S, v10.4S
        add         v10.4S, v6.4S, v9.4S
        add         v9.4S,  v5.4S, v8.4S
        add         v8.4S,  v4.4S, v7.4S
.endm

.macro tr16_begin in0, in1, in2, in3, in4, in5, in6, in7
        smull       v2.4S,  \in0\().4H, v1.H[1]
        smull       v3.4S,  \in0\().4H, v1.H[0]
        smull       v4.4S,  \in0\().4H, v1.H[3]
        smull       v5.4S,  \in0\().4H, v1.H[2]
        smull       v6.4S,  \in0\().4H, v1.H[5]
        smull       v7.4S,  \in0\().4H, v1.H[4]
        smull       v8.4S,  \in0\().4H, v1.H[7]
        smull       v9.4S,  \in0\().4H, v1.H[6]
        smlal       v2.4S,  \in1\().4H, v1.H[0]
        smlal       v3.4S,  \in1\().4H, v1.H[5]
        smlal       v4.4S,  \in1\().4H, v1.H[6]
        smlsl       v5.4S,  \in1\().4H, v1.H[4]
        smlsl       v6.4S,  \in1\().4H, v1.H[3]
        smlsl       v7.4S,  \in1\().4H, v1.H[1]
        smlsl       v8.4S,  \in1\().4H, v1.H[2]
        smlsl       v9.4S,  \in1\().4H, v1.H[7]
        smlal       v2.4S,  \in2\().4H, v1.H[3]
        smlal       v3.4S,  \in2\().4H, v1.H[6]
        smlsl       v4.4S,  \in2\().4H, v1.H[2]
        smlsl       v5.4S,  \in2\().4H, v1.H[0]
        smlsl       v6.4S,  \in2\().4H, v1.H[7]
        smlal       v7.4S,  \in2\().4H, v1.H[5]
        smlal       v8.4S,  \in2\().4H, v1.H[1]
        smlal       v9.4S,  \in2\().4H, v1.H[4]
        smlal       v2.4S,  \in3\().4H, v1.H[2]
        smlsl       v3.4S,  \in3\().4H, v1.H[4]
        smlsl       v4.4S,  \in3\().4H, v1.H[0]
        smlal       v5.4S,  \in3\().4H, v1.H[6]
        smlal       v6.4S,  \in3\().4H, v1.H[1]
        smlal       v7.4S,  \in3\().4H, v1.H[7]
        smlsl       v8.4S,  \in3\().4H, v1.H[3]
        smlsl       v9.4S,  \in3\().4H, v1.H[5]
        smlal       v2.4S,  \in4\().4H, v1.H[5]
        smlsl       v3.4S,  \in4\().4H, v1.H[3]
        smlsl       v4.4S,  \in4\().4H, v1.H[7]
        smlal       v5.4S,  \in4\().4H, v1.H[1]
        smlsl       v6.4S,  \in4\().4H, v1.H[6]
        smlsl       v7.4S,  \in4\().4H, v1.H[0]
        smlal       v8.4S,  \in4\().4H, v1.H[4]
        smlal       v9.4S,  \in4\().4H, v1.H[2]
        smlal       v2.4S,  \in5\().4H, v1.H[4]
        smlsl       v3.4S,  \in5\().4H, v1.H[1]
        smlal       v4.4S,  \in5\().4H, v1.H[5]
        smlal       v5.4S,  \in5\().4H, v1.H[7]
        smlsl       v6.4S,  \in5\().4H, v1.H[0]
        smlal       v7.4S,  \in5\().4H, v1.H[2]
        smlal       v8.4S,  \in5\().4H, v1.H[6]
        smlsl       v9.4S,  \in5\().4H, v1.H[3]
        smlal       v2.4S,  \in6\().4H, v1.H[7]
        smlsl       v3.4S,  \in6\().4H, v1.H[2]
        smlal       v4.4S,  \in6\().4H, v1.H[1]
        smlsl       v5.4S,  \in6\().4H, v1.H[3]
        smlal       v6.4S,  \in6\().4H, v1.H[4]
        smlal       v7.4S,  \in6\().4H, v1.H[6]
        smlsl       v8.4S,  \in6\().4H, v1.H[5]
        smlal       v9.4S,  \in6\().4H, v1.H[0]
        smlal       v2.4S,  \in7\().4H, v1.H[6]
        smlsl       v3.4S,  \in7\().4H, v1.H[7]
        smlal       v4.4S,  \in7\().4H, v1.H[4]
        smlsl       v5.4S,  \in7\().4H, v1.H[5]
        smlal       v6.4S,  \in7\().4H, v1.H[2]
        smlsl       v7.4S,  \in7\().4H, v1.H[3]
        smlal       v8.4S,  \in7\().4H, v1.H[0]
        smlsl       v9.4S,  \in7\().4H, v1.H[1]
.endm

.macro  tr32_begin
        smull       v6.4S,  v16.4H, v0.H[0]
        smull       v7.4S,  v16.4H, v0.H[0]
        smull       v8.4S,  v16.4H, v0.H[2]
        smull       v9.4S,  v16.4H, v0.H[3]
        smlal       v6.4S,  v17.4H, v0.H[0]
        smlal       v7.4S,  v17.4H, v0.H[4]
        smlal       v8.4S,  v17.4H, v0.H[7]
        smlal       v9.4S,  v17.4H, v1.H[2]
        smlal       v6.4S,  v18.4H, v0.H[2]
        smlal       v7.4S,  v18.4H, v0.H[7]
        smlal       v8.4S,  v18.4H, v1.H[4]
        smlsl       v9.4S,  v18.4H, v1.H[6]
        smlal       v6.4S,  v19.4H, v0.H[3]
        smlal       v7.4S,  v19.4H, v1.H[2]
        smlsl       v8.4S,  v19.4H, v1.H[6]
        smlsl       v9.4S,  v19.4H, v0.H[7]
        smlal       v6.4S,  v20.4H, v0.H[4]
        smlal       v7.4S,  v20.4H, v1.H[5]
        smlsl       v8.4S,  v20.4H, v1.H[1]
        smlsl       v9.4S,  v20.4H, v0.H[0]
        smlal       v6.4S,  v21.4H, v0.H[5]
        smlsl       v7.4S,  v21.4H, v1.H[7]
        smlsl       v8.4S,  v21.4H, v0.H[4]
        smlsl       v9.4S,  v21.4H, v0.H[6]
        smlal       v6.4S,  v22.4H, v0.H[6]
        smlsl       v7.4S,  v22.4H, v1.H[4]
        smlsl       v8.4S,  v22.4H, v0.H[0]
        smlsl       v9.4S,  v22.4H, v1.H[5]
        smlal       v6.4S,  v23.4H, v0.H[7]
        smlsl       v7.4S,  v23.4H, v1.H[1]
        smlsl       v8.4S,  v23.4H, v0.H[5]
        smlal       v9.4S,  v23.4H, v1.H[3]
        smlal       v6.4S,  v24.4H, v1.H[0]
        smlsl       v7.4S,  v24.4H, v0.H[6]
        smlsl       v8.4S,  v24.4H, v1.H[2]
        smlal       v9.4S,  v24.4H, v0.H[4]
        smlal       v6.4S,  v25.4H, v1.H[1]
        smlsl       v7.4S,  v25.4H, v0.H[3]
        smlsl       v8.4S,  v25.4H, v1.H[7]
        smlal       v9.4S,  v25.4H, v0.H[2]
        smlal       v6.4S,  v26.4H, v1.H[2]
        smlsl       v7.4S,  v26.4H, v0.H[0]
        smlal       v8.4S,  v26.4H, v1.H[3]
        smlal       v9.4S,  v26.4H, v1.H[1]
        smlal       v6.4S,  v27.4H, v1.H[3]
        smlsl       v7.4S,  v27.4H, v0.H[2]
        smlal       v8.4S,  v27.4H, v0.H[6]
        smlsl       v9.4S,  v27.4H, v1.H[7]
        smlal       v6.4S,  v28.4H, v1.H[4]
        smlsl       v7.4S,  v28.4H, v0.H[5]
        smlal       v8.4S,  v28.4H, v0.H[0]
        smlsl       v9.4S,  v28.4H, v1.H[0]
        smlal       v6.4S,  v29.4H, v1.H[5]
        smlsl       v7.4S,  v29.4H, v1.H[0]
        smlal       v8.4S,  v29.4H, v0.H[3]
        smlsl       v9.4S,  v29.4H, v0.H[0]
        smlal       v6.4S,  v30.4H, v1.H[6]
        smlsl       v7.4S,  v30.4H, v1.H[3]
        smlal       v8.4S,  v30.4H, v1.H[0]
        smlsl       v9.4S,  v30.4H, v0.H[5]
        smlal       v6.4S,  v31.4H, v1.H[7]
        smlsl       v7.4S,  v31.4H, v1.H[6]
        smlal       v8.4S,  v31.4H, v1.H[5]
        smlsl       v9.4S,  v31.4H, v1.H[0]
        smull       v2.4S,  v16.4H, v0.H[4]
        smull       v3.4S,  v16.4H, v0.H[5]
        smull       v4.4S,  v16.4H, v0.H[6]
        smull       v5.4S,  v16.4H, v0.H[7]
        smlal       v2.4S,  v17.4H, v1.H[5]
        smlsl       v3.4S,  v17.4H, v1.H[7]
        smlsl       v4.4S,  v17.4H, v1.H[4]
        smlsl       v5.4S,  v17.4H, v1.H[1]
        smlsl       v2.4S,  v18.4H, v1.H[1]
        smlsl       v3.4S,  v18.4H, v0.H[4]
        smlsl       v4.4S,  v18.4H, v0.H[0]
        smlsl       v5.4S,  v18.4H, v0.H[5]
        smlsl       v2.4S,  v19.4H, v0.H[0]
        smlsl       v3.4S,  v19.4H, v0.H[6]
        smlsl       v4.4S,  v19.4H, v1.H[5]
        smlal       v5.4S,  v19.4H, v1.H[3]
        smlsl       v2.4S,  v20.4H, v1.H[0]
        smlal       v3.4S,  v20.4H, v1.H[6]
        smlal       v4.4S,  v20.4H, v0.H[5]
        smlal       v5.4S,  v20.4H, v0.H[3]
        smlal       v2.4S,  v21.4H, v1.H[6]
        smlal       v3.4S,  v21.4H, v0.H[3]
        smlal       v4.4S,  v21.4H, v0.H[7]
        smlsl       v5.4S,  v21.4H, v1.H[5]
        smlal       v2.4S,  v22.4H, v0.H[5]
        smlal       v3.4S,  v22.4H, v0.H[7]
        smlsl       v4.4S,  v22.4H, v1.H[3]
        smlsl       v5.4S,  v22.4H, v0.H[0]
        smlal       v2.4S,  v23.4H, v0.H[3]
        smlsl       v3.4S,  v23.4H, v1.H[5]
        smlsl       v4.4S,  v23.4H, v0.H[0]
        smlal       v5.4S,  v23.4H, v1.H[7]
        smlal       v2.4S,  v24.4H, v1.H[4]
        smlsl       v3.4S,  v24.4H, v0.H[2]
        smlsl       v4.4S,  v24.4H, v1.H[6]
        smlal       v5.4S,  v24.4H, v0.H[0]
        smlsl       v2.4S,  v25.4H, v1.H[2]
        smlsl       v3.4S,  v25.4H, v1.H[0]
        smlal       v4.4S,  v25.4H, v0.H[4]
        smlal       v5.4S,  v25.4H, v1.H[6]
        smlsl       v2.4S,  v26.4H, v0.H[0] 
        smlal       v3.4S,  v26.4H, v1.H[4]
        smlal       v4.4S,  v26.4H, v1.H[0]
        smlsl       v5.4S,  v26.4H, v0.H[2]
        smlsl       v2.4S,  v27.4H, v0.H[7]
        smlal       v3.4S,  v27.4H, v0.H[0]
        smlsl       v4.4S,  v27.4H, v1.H[2]
        smlsl       v5.4S,  v27.4H, v1.H[4]
        smlal       v2.4S,  v28.4H, v1.H[7]
        smlal       v3.4S,  v28.4H, v1.H[1]
        smlsl       v4.4S,  v28.4H, v0.H[2]
        smlal       v5.4S,  v28.4H, v0.H[4]
        smlal       v2.4S,  v29.4H, v0.H[6]
        smlsl       v3.4S,  v29.4H, v1.H[3]
        smlsl       v4.4S,  v29.4H, v1.H[7]
        smlal       v5.4S,  v29.4H, v1.H[2]
        smlal       v2.4S,  v30.4H, v0.H[2]
        smlsl       v3.4S,  v30.4H, v0.H[0]
        smlal       v4.4S,  v30.4H, v0.H[3]
        smlsl       v5.4S,  v30.4H, v0.H[6]
        smlal       v2.4S,  v31.4H, v1.H[3]
        smlsl       v3.4S,  v31.4H, v1.H[2]
        smlal       v4.4S,  v31.4H, v1.H[1]
        smlsl       v5.4S,  v31.4H, v1.H[0]
        st1         {v6.2D - v9.2D}, [x6], #64
        st1         {v2.2D - v5.2D}, [x6], #64

        smull       v6.4S,  v16.4H, v1.H[0]
        smull       v7.4S,  v16.4H, v1.H[1]
        smull       v8.4S,  v16.4H, v1.H[2]
        smull       v9.4S,  v16.4H, v1.H[3]
        smlsl       v6.4S,  v17.4H, v0.H[6]
        smlsl       v7.4S,  v17.4H, v0.H[3]
        smlsl       v8.4S,  v17.4H, v0.H[0]
        smlsl       v9.4S,  v17.4H, v0.H[2]
        smlsl       v6.4S,  v18.4H, v1.H[2]
        smlsl       v7.4S,  v18.4H, v1.H[7]
        smlal       v8.4S,  v18.4H, v1.H[3]
        smlal       v9.4S,  v18.4H, v0.H[6]
        smlal       v6.4S,  v19.4H, v0.H[4]
        smlal       v7.4S,  v19.4H, v0.H[2]
        smlal       v8.4S,  v19.4H, v1.H[1]
        smlsl       v9.4S,  v19.4H, v1.H[7]
        smlal       v6.4S,  v20.4H, v1.H[4]
        smlsl       v7.4S,  v20.4H, v1.H[2]
        smlsl       v8.4S,  v20.4H, v0.H[0]
        smlsl       v9.4S,  v20.4H, v0.H[7]
        smlsl       v6.4S,  v21.4H, v0.H[2]
        smlsl       v7.4S,  v21.4H, v1.H[0]
        smlal       v8.4S,  v21.4H, v1.H[4]
        smlal       v9.4S,  v21.4H, v0.H[0]
        smlsl       v6.4S,  v22.4H, v1.H[6]
        smlal       v7.4S,  v22.4H, v0.H[4]
        smlal       v8.4S,  v22.4H, v1.H[0]
        smlsl       v9.4S,  v22.4H, v1.H[2]
        smlal       v6.4S,  v23.4H, v0.H[0]
        smlal       v7.4S,  v23.4H, v1.H[6]
        smlsl       v8.4S,  v23.4H, v0.H[2]
        smlal       v9.4S,  v23.4H, v1.H[4]
        smlsl       v6.4S,  v24.4H, v1.H[7]
        smlsl       v7.4S,  v24.4H, v0.H[0]
        smlal       v8.4S,  v24.4H, v1.H[5]
        smlal       v9.4S,  v24.4H, v0.H[3]
        smlsl       v6.4S,  v25.4H, v0.H[0]
        smlal       v7.4S,  v25.4H, v1.H[3]
        smlal       v8.4S,  v25.4H, v0.H[7]
        smlsl       v9.4S,  v25.4H, v0.H[5]
        smlal       v6.4S,  v26.4H, v1.H[5]
        smlal       v7.4S,  v26.4H, v0.H[7]
        smlsl       v8.4S,  v26.4H, v0.H[3]
        smlal       v9.4S,  v26.4H, v1.H[6]
        smlal       v6.4S,  v27.4H, v0.H[3]
        smlsl       v7.4S,  v27.4H, v0.H[5]
        smlal       v8.4S,  v27.4H, v1.H[6]
        smlal       v9.4S,  v27.4H, v1.H[0]
        smlsl       v6.4S,  v28.4H, v1.H[3]
        smlsl       v7.4S,  v28.4H, v1.H[5]
        smlal       v8.4S,  v28.4H, v0.H[6]
        smlsl       v9.4S,  v28.4H, v0.H[0]
        smlsl       v6.4S,  v29.4H, v0.H[5]
        smlal       v7.4S,  v29.4H, v0.H[0]
        smlsl       v8.4S,  v29.4H, v0.H[4]
        smlal       v9.4S,  v29.4H, v1.H[1]
        smlal       v6.4S,  v30.4H, v1.H[1]
        smlsl       v7.4S,  v30.4H, v1.H[4]
        smlal       v8.4S,  v30.4H, v1.H[7]
        smlal       v9.4S,  v30.4H, v1.H[5]
        smlal       v6.4S,  v31.4H, v0.H[7]
        smlsl       v7.4S,  v31.4H, v0.H[6]
        smlal       v8.4S,  v31.4H, v0.H[5]
        smlsl       v9.4S,  v31.4H, v0.H[4] 
        smull       v2.4S,  v16.4H, v1.H[4]
        smull       v3.4S,  v16.4H, v1.H[5]
        smull       v4.4S,  v16.4H, v1.H[6]
        smull       v5.4S,  v16.4H, v1.H[7]
        smlsl       v2.4S,  v17.4H, v0.H[5]
        smlsl       v3.4S,  v17.4H, v1.H[0]
        smlsl       v4.4S,  v17.4H, v1.H[3]
        smlsl       v5.4S,  v17.4H, v1.H[6]
        smlal       v2.4S,  v18.4H, v0.H[0]
        smlal       v3.4S,  v18.4H, v0.H[3]
        smlal       v4.4S,  v18.4H, v1.H[0]
        smlal       v5.4S,  v18.4H, v1.H[5]
        smlsl       v2.4S,  v19.4H, v1.H[0]
        smlsl       v3.4S,  v19.4H, v0.H[0]
        smlsl       v4.4S,  v19.4H, v0.H[5]
        smlsl       v5.4S,  v19.4H, v1.H[4]
        smlal       v2.4S,  v20.4H, v1.H[7]
        smlal       v3.4S,  v20.4H, v0.H[6]
        smlal       v4.4S,  v20.4H, v0.H[2]
        smlal       v5.4S,  v20.4H, v1.H[3]
        smlal       v2.4S,  v21.4H, v1.H[1]
        smlsl       v3.4S,  v21.4H, v1.H[3]
        smlsl       v4.4S,  v21.4H, v0.H[0]
        smlsl       v5.4S,  v21.4H, v1.H[2]
        smlsl       v2.4S,  v22.4H, v0.H[2]
        smlsl       v3.4S,  v22.4H, v1.H[7]
        smlal       v4.4S,  v22.4H, v0.H[3]
        smlal       v5.4S,  v22.4H, v1.H[1]
        smlal       v2.4S,  v23.4H, v0.H[4]
        smlal       v3.4S,  v23.4H, v1.H[2]
        smlsl       v4.4S,  v23.4H, v0.H[6]
        smlsl       v5.4S,  v23.4H, v1.H[0]
        smlsl       v2.4S,  v24.4H, v1.H[3]
        smlsl       v3.4S,  v24.4H, v0.H[5]
        smlal       v4.4S,  v24.4H, v1.H[1]
        smlal       v5.4S,  v24.4H, v0.H[7]
        smlsl       v2.4S,  v25.4H, v1.H[5]
        smlal       v3.4S,  v25.4H, v0.H[0]
        smlsl       v4.4S,  v25.4H, v1.H[4]
        smlsl       v5.4S,  v25.4H, v0.H[6]
        smlal       v2.4S,  v26.4H, v0.H[6]
        smlsl       v3.4S,  v26.4H, v0.H[4]
        smlal       v4.4S,  v26.4H, v1.H[7]
        smlal       v5.4S,  v26.4H, v0.H[5]
        smlsl       v2.4S,  v27.4H, v0.H[0]
        smlal       v3.4S,  v27.4H, v1.H[1]
        smlal       v4.4S,  v27.4H, v1.H[5]
        smlsl       v5.4S,  v27.4H, v0.H[4]
        smlal       v2.4S,  v28.4H, v0.H[7]
        smlsl       v3.4S,  v28.4H, v1.H[6]
        smlsl       v4.4S,  v28.4H, v1.H[2]
        smlal       v5.4S,  v28.4H, v0.H[3]
        smlsl       v2.4S,  v29.4H, v1.H[6]
        smlsl       v3.4S,  v29.4H, v1.H[4]
        smlal       v4.4S,  v29.4H, v0.H[7]
        smlsl       v5.4S,  v29.4H, v0.H[2]
        smlsl       v2.4S,  v30.4H, v1.H[2]
        smlal       v3.4S,  v30.4H, v0.H[7]
        smlsl       v4.4S,  v30.4H, v0.H[4]
        smlal       v5.4S,  v30.4H, v0.H[0]
        smlal       v2.4S,  v31.4H, v0.H[3]
        smlsl       v3.4S,  v31.4H, v0.H[2]
        smlal       v4.4S,  v31.4H, v0.H[0]
        smlsl       v5.4S,  v31.4H, v0.H[0]
        st1         {v6.2D - v9.2D}, [x6], #64
        st1         {v2.2D - v5.2D}, [x6], #64
.endm

function ff_hevc_transform_8x8_neon_8, export=1
        movrel      x3, trans_coeff
        mov         x5, #16
        mov         x6, x0
        ld1         {v0.2D}, [x3]
        ld1         {v24.1D}, [x0], x5
        ld1         {v25.1D}, [x0], x5
        ld1         {v26.1D}, [x0], x5
        ld1         {v27.1D}, [x0], x5
        ld1         {v28.1D}, [x0], x5
        ld1         {v29.1D}, [x0], x5
        ld1         {v30.1D}, [x0], x5
        ld1         {v31.1D}, [x0], x5
        mov         x0, x6
        tr8_begin   v25, v27, v29, v31
        tr4         v24, v26, v28, v30
        tr8_end     #7
        st1         {v2.1D}, [x0], x5
        st1         {v3.1D}, [x0], x5
        st1         {v4.1D}, [x0], x5
        st1         {v5.1D}, [x0], x5
        st1         {v6.1D}, [x0], x5
        st1         {v7.1D}, [x0], x5
        st1         {v8.1D}, [x0], x5
        st1         {v9.1D}, [x0], x5
        mov         x0, x6
        cmp         x1, #4
        b.lt        1f
        add         x0, x0, #8
        ld1         {v24.1D}, [x0], x5
        ld1         {v25.1D}, [x0], x5
        ld1         {v26.1D}, [x0], x5
        ld1         {v27.1D}, [x0], x5
        ld1         {v28.1D}, [x0], x5
        ld1         {v29.1D}, [x0], x5
        ld1         {v30.1D}, [x0], x5
        ld1         {v31.1D}, [x0], x5
        sub         x0, x0, #128
        tr8_begin   v25, v27, v29, v31
        tr4         v24, v26, v28, v30
        tr8_end     #7
        st1         {v2.1D}, [x0], x5
        st1         {v3.1D}, [x0], x5
        st1         {v4.1D}, [x0], x5
        st1         {v5.1D}, [x0], x5
        st1         {v6.1D}, [x0], x5
        st1         {v7.1D}, [x0], x5
        st1         {v8.1D}, [x0], x5
        st1         {v9.1D}, [x0], x5
        mov         x0, x6
1:      ld1         {v24.1D - v27.1D}, [x0], #32
        ld1         {v28.1D - v31.1D}, [x0]
        mov         x0, x6
        transpose_16b_4x4 v24, v26, v28, v30, v11, v12, v13, v14
        transpose_16b_4x4 v25, v27, v29, v31, v11, v12, v13, v14
        tr8_begin   v26, v30, v27, v31
        tr4         v24, v28, v25, v29
        tr8_end     #12
        transpose_16b_4x4 v2, v3, v4, v5, v11, v12, v13, v14
        transpose_16b_4x4 v6, v7, v8, v9, v11, v12, v13, v14
        zip1        v11.2D, v2.2D, v6.2D
        zip1        v12.2D, v3.2D, v7.2D
        zip1        v13.2D, v4.2D, v8.2D
        zip1        v14.2D, v5.2D, v9.2D
        st1         {v11.2D - v14.2D}, [x0], #64
        ld1         {v24.4H - v27.4H}, [x0], #32
        ld1         {v28.4H - v31.4H}, [x0]
        sub         x0, x0, #32
        transpose_16b_4x4 v24, v26, v28, v30, v11, v12, v13, v14
        transpose_16b_4x4 v25, v27, v29, v31, v11, v12, v13, v14
        tr8_begin   v26, v30, v27, v31
        tr4         v24, v28, v25, v29
        tr8_end     #12 
        transpose_16b_4x4 v2, v3, v4, v5, v11, v12, v13, v14
        transpose_16b_4x4 v6, v7, v8, v9, v11, v12, v13, v14
        zip1        v11.2D, v2.2D, v6.2D
        zip1        v12.2D, v3.2D, v7.2D
        zip1        v13.2D, v4.2D, v8.2D
        zip1        v14.2D, v5.2D, v9.2D
        st1         {v11.2D - v12.2D}, [x0], #32
        st1         {v13.2D - v14.2D}, [x0], #32
        sub         x0, x0, #64
        ret
endfunc

function ff_hevc_transform_16x16_neon_8, export=1
        mov         x5, #32
        lsr         x6, x5, #1
        add         x7, x1, #4
        cmp         x1, #12
        csel        x1, x6, x7, gt
        movrel      x3, trans_coeff
        mov         x10, sp
        mov         x4, XZR
0:      ld1         {v0.2D - v1.2D}, [x3]
        add         x0, x0, x5
        lsl         x5, x5, #1
        ld1         {v24.1D}, [x0], x5
        ld1         {v25.1D}, [x0], x5
        ld1         {v26.1D}, [x0], x5
        ld1         {v27.1D}, [x0], x5
        ld1         {v28.1D}, [x0], x5
        ld1         {v29.1D}, [x0], x5
        ld1         {v30.1D}, [x0], x5
        ld1         {v31.1D}, [x0], x5
        sub         x0, x0, x5, lsl #3
        sub         x0, x0, x5, lsr #1
        tr16_begin  v24, v25, v26, v27, v28, v29, v30, v31
        sub         x10, x10, #128
        st1         {v2.2D - v5.2D}, [x10], #64
        st1         {v6.2D - v9.2D}, [x10], #64
        ld1         {v24.1D}, [x0], x5
        ld1         {v25.1D}, [x0], x5
        ld1         {v26.1D}, [x0], x5
        ld1         {v27.1D}, [x0], x5
        ld1         {v28.1D}, [x0], x5
        ld1         {v29.1D}, [x0], x5
        ld1         {v30.1D}, [x0], x5
        ld1         {v31.1D}, [x0], x5
        sub         x0, x0, x5, lsl #3
        lsr         x5, x5, #1
        tr8_begin   v25, v27, v29, v31
        tr4         v24, v26, v28, v30
        tr8_end_0 
        sub         x10, x10, #128
        ld1         {v0.2D - v3.2D}, [x10], #64
        add         v4.4S, v8.4S, v0.4S
        sub         v8.4S, v8.4S, v0.4S
        sqrshrn     v5.4H, v4.4S, #7
        st1         {v5.1D}, [x0], x5
        add         v4.4S, v9.4S, v1.4S
        sub         v9.4S, v9.4S, v1.4S
        sqrshrn     v5.4H, v4.4S, #7
        st1         {v5.1D}, [x0], x5
        add         v4.4S, v10.4S, v2.4S
        sub         v10.4S, v10.4S, v2.4S
        sqrshrn     v5.4H, v4.4S, #7
        st1         {v5.1D}, [x0], x5
        add         v4.4S, v11.4S, v3.4S
        sub         v11.4S, v11.4S, v3.4S
        sqrshrn     v5.4H, v4.4S, #7
        st1         {v5.1D}, [x0], x5
        ld1         {v0.2D - v3.2D}, [x10], #64
        add         v4.4S, v12.4S, v0.4S
        sub         v12.4S, v12.4S, v0.4S
        sqrshrn     v5.4H, v4.4S, #7
        st1         {v5.1D}, [x0], x5
        add         v4.4S, v13.4S, v1.4S
        sub         v13.4S, v13.4S, v1.4S
        sqrshrn     v5.4H, v4.4S, #7
        st1         {v5.1D}, [x0], x5
        add         v4.4S, v14.4S, v2.4S
        sub         v14.4S, v14.4S, v2.4S
        sqrshrn     v5.4H, v4.4S, #7
        st1         {v5.1D}, [x0], x5
        add         v4.4S, v15.4S, v3.4S
        sub         v15.4S, v15.4S, v3.4S
        sqrshrn     v5.4H, v4.4S, #7
        st1         {v5.1D}, [x0], x5
        sqrshrn     v5.4H, v15.4S, #7
        st1         {v5.1D}, [x0], x5
        sqrshrn     v5.4H, v14.4S, #7
        st1         {v5.1D}, [x0], x5
        sqrshrn     v5.4H, v13.4S, #7
        st1         {v5.1D}, [x0], x5
        sqrshrn     v5.4H, v12.4S, #7
        st1         {v5.1D}, [x0], x5
        sqrshrn     v5.4H, v11.4S, #7
        st1         {v5.1D}, [x0], x5
        sqrshrn     v5.4H, v10.4S, #7
        st1         {v5.1D}, [x0], x5
        sqrshrn     v5.4H, v9.4S, #7
        st1         {v5.1D}, [x0], x5
        sqrshrn     v5.4H, v8.4S, #7
        st1         {v5.1D}, [x0], x5
        sub         x0, x0, x5, lsl #4
        add         x0, x0, #8
        add         x4, x4, #4
        cmp         x4, x1
        b.lt        0b
        sub         x0, x0, x4, lsl #1
        mov         x4, #4
1:      ld1         {v0.2D - v1.2D}, [x3]
        ld1         {v16.1D - v19.1D}, [x0], #32
        ld1         {v20.1D - v23.1D}, [x0], #32
        ld1         {v24.1D - v27.1D}, [x0], #32
        ld1         {v28.1D - v31.1D}, [x0], #32
        sub         x0, x0, #128
        transpose_16b_4x4   v16, v20, v24, v28, v11, v12, v13, v14
        transpose_16b_4x4   v17, v21, v25, v29, v11, v12, v13, v14
        transpose_16b_4x4   v18, v22, v26, v30, v11, v12, v13, v14
        transpose_16b_4x4   v19, v23, v27, v31, v11, v12, v13, v14
        tr16_begin  v20, v28, v21, v29, v22, v30, v23, v31
        sub         x10, x10, #128
        st1         {v2.2D - v5.2D}, [x10], #64
        st1         {v6.2D - v9.2D}, [x10], #64
        tr8_begin   v24, v25, v26, v27
        tr4         v16, v17, v18, v19
        tr8_end_0
        sub         x10, x10, #128
        ld1         {v0.2D - v3.2D}, [x10], #64
        add         v4.4S, v8.4S, v0.4S
        sub         v18.4S, v8.4S, v0.4S
        sqrshrn     v0.4H, v4.4S, #12
        add         v4.4S, v9.4S, v1.4S
        sub         v19.4S, v9.4S, v1.4S
        sqrshrn     v1.4H, v4.4S, #12
        add         v4.4S, v10.4S, v2.4S
        sub         v20.4S, v10.4S, v2.4S
        sqrshrn     v2.4H, v4.4S, #12
        add         v4.4S, v11.4S, v3.4S
        sub         v21.4S, v11.4S, v3.4S
        sqrshrn     v3.4H, v4.4S, #12
        ld1         {v4.2D - v7.2D}, [x10], #64
        add         v10.4S, v12.4S, v4.4S
        sub         v22.4S, v12.4S, v4.4S
        sqrshrn     v4.4H, v10.4S, #12
        add         v10.4S, v13.4S, v5.4S
        sub         v23.4S, v13.4S, v5.4S
        sqrshrn     v5.4H, v10.4S, #12
        add         v10.4S, v14.4S, v6.4S
        sub         v24.4S, v14.4S, v6.4S
        sqrshrn     v6.4H, v10.4S, #12
        add         v10.4S, v15.4S, v7.4S 
        sub         v25.4S, v15.4S, v7.4S
        sqrshrn     v7.4H, v10.4S, #12
        sqrshrn     v8.4H, v25.4S, #12
        sqrshrn     v9.4H, v24.4S, #12
        sqrshrn     v10.4H, v23.4S, #12
        sqrshrn     v11.4H, v22.4S, #12
        sqrshrn     v12.4H, v21.4S, #12
        sqrshrn     v13.4H, v20.4S, #12
        sqrshrn     v14.4H, v19.4S, #12
        sqrshrn     v15.4H, v18.4S, #12
        transpose_16b_4x4 v0, v1, v2, v3, v20, v21, v22, v23
        transpose_16b_4x4 v4, v5, v6, v7, v20, v21, v22, v23
        transpose_16b_4x4 v8, v9, v10, v11, v20, v21, v22, v23
        transpose_16b_4x4 v12, v13, v14, v15, v20, v21, v22, v23
        trn1        v20.2D, v0.2D, v4.2D                    //vswp      d9, d12
        trn1        v21.2D, v8.2D, v12.2D                   //vswp      d3, d6
        trn1        v22.2D, v1.2D, v5.2D                    //vswp      d3, d9
        trn1        v23.2D, v9.2D, v13.2D                   //vswp      d6, d12
        trn1        v24.2D, v2.2D, v6.2D                    //vswp      d1, d4
        trn1        v25.2D, v10.2D, v14.2D                  //vswp      d11, d14
        trn1        v26.2D, v3.2D, v7.2D                    //vswp      d2, d8
        trn1        v27.2D, v11.2D, v15.2D                  //vswp      d7, d13
        st1         {v20.2D - v23.2D}, [x0], #64
        st1         {v24.2D - v27.2D}, [x0], #64
        subs        x4, x4, #1
        b.ne        1b
        ret
endfunc

.macro  write32_buffer
        st1         {v0.1D - v3.1D}, [x8], #32
        st1         {v4.1D - v7.1D}, [x8], #32
        st1         {v8.1D - v11.1D}, [x8], #32
        st1         {v12.1D - v15.1D}, [x8], #32
        st1         {v16.1D - v19.1D}, [x8], #32
        st1         {v20.1D - v23.1D}, [x8], #32
        st1         {v24.1D - v27.1D}, [x8], #32
        st1         {v28.1D - v31.1D}, [x8], #32
.endm

.macro  tr32_out tmp, dst_first, dst_last, shift
        ld1         {v12.2D}, [x6], #16
        ld1         {v13.2D}, [x7], #16
        add         \tmp\().4S, v12.4S, v13.4S
        sub         v13.4S, v13.4S, v12.4S
        sqrshrn     \dst_first\().4H, \tmp\().4S, \shift
        sqrshrn     \dst_last\().4H, v13.4S, \shift
.endm

.macro  tr32_transform_func shift, action, limit
        mov         x4, XZR
        mov         x10, x8
0:      add         x3, x3, #32
        ld1         {v0.2D - v1.2D}, [x3]
        sub         x3, x3, #32
        add         x0, x0, x5
        lsl         x5, x5, #1
        ld1         {v16.1D}, [x0], x5
        ld1         {v17.1D}, [x0], x5
        ld1         {v18.1D}, [x0], x5
        ld1         {v19.1D}, [x0], x5
        ld1         {v20.1D}, [x0], x5
        ld1         {v21.1D}, [x0], x5
        ld1         {v22.1D}, [x0], x5
        ld1         {v23.1D}, [x0], x5
        ld1         {v24.1D}, [x0], x5
        ld1         {v25.1D}, [x0], x5
        ld1         {v26.1D}, [x0], x5
        ld1         {v27.1D}, [x0], x5
        ld1         {v28.1D}, [x0], x5
        ld1         {v29.1D}, [x0], x5
        ld1         {v30.1D}, [x0], x5
        ld1         {v31.1D}, [x0], x5
        sub         x0, x0, x5, lsl #4
        tr32_begin
        add         x0, x0, x5, lsr #1
        sub         x6, x6, x5, lsl #1
        ld1         {v0.2D - v1.2D}, [x3]
        lsl         x5, x5, #1
        ld1         {v24.1D}, [x0], x5 
        ld1         {v25.1D}, [x0], x5
        ld1         {v26.1D}, [x0], x5
        ld1         {v27.1D}, [x0], x5
        ld1         {v28.1D}, [x0], x5
        ld1         {v29.1D}, [x0], x5
        ld1         {v30.1D}, [x0], x5
        ld1         {v31.1D}, [x0], x5
        sub         x0, x0, x5, lsl #3
        sub         x0, x0, x5, lsr #1
        tr16_begin  v24, v25, v26, v27, v28, v29, v30, v31
        st1         {v2.2D - v5.2D}, [x9], #64
        st1         {v6.2D - v9.2D}, [x9], #64
        sub         x9, x9, #128
        ld1         {v24.1D}, [x0], x5
        ld1         {v25.1D}, [x0], x5
        ld1         {v26.1D}, [x0], x5
        ld1         {v27.1D}, [x0], x5
        ld1         {v28.1D}, [x0], x5
        ld1         {v29.1D}, [x0], x5
        ld1         {v30.1D}, [x0], x5
        ld1         {v31.1D}, [x0], x5
        sub         x0, x0, x5, lsl #3
        tr8_begin   v25, v27, v29, v31
        tr4         v24, v26, v28, v30
        tr8_end_0
        ld1         {v0.2D - v3.2D}, [x9], #64
        add         v4.4S, v8.4S, v0.4S
        sub         v8.4S, v8.4S, v0.4S
        add         v5.4S, v9.4S, v1.4S
        sub         v9.4S, v9.4S, v1.4S
        add         v6.4S, v10.4S, v2.4S
        sub         v10.4S, v10.4S, v2.4S
        add         v7.4S, v11.4S, v3.4S
        sub         v11.4S, v11.4S, v3.4S
        st1         {v4.2D - v7.2D}, [x7], #64
        ld1         {v0.2D - v3.2D}, [x9], #64
        sub         x9, x9, #128
        add         v4.4S, v12.4S, v0.4S
        sub         v12.4S, v12.4S, v0.4S
        add         v5.4S, v13.4S, v1.4S
        sub         v13.4S, v13.4S, v1.4S
        add         v6.4S, v14.4S, v2.4S
        sub         v14.4S, v14.4S, v2.4S
        add         v7.4S, v15.4S, v3.4S
        sub         v15.4S, v15.4S, v3.4S
        st1         {v4.2D - v7.2D}, [x7], #64
        st1         {v15.2D}, [x7], #16
        st1         {v14.2D}, [x7], #16
        st1         {v13.2D}, [x7], #16
        st1         {v12.2D}, [x7], #16
        st1         {v11.2D}, [x7], #16
        st1         {v10.2D}, [x7], #16
        st1         {v9.2D}, [x7], #16
        st1         {v8.2D}, [x7], #16
        sub         x7, x7, x5
        add         x0, x0, #8
        lsr         x5, x5, #2
        tr32_out    v14, v0, v28, \shift
        tr32_out    v14, v8, v29, \shift
        tr32_out    v14, v16, v30, \shift
        tr32_out    v14, v24, v31, \shift
        st1         {v28.1D - v31.1D}, [x9], #32
        tr32_out    v14, v1, v28, \shift
        tr32_out    v14, v9, v29, \shift
        tr32_out    v14, v17, v30, \shift
        tr32_out    v14, v25, v31, \shift
        st1         {v28.1D - v31.1D}, [x9], #32
        tr32_out    v14, v2, v28, \shift
        tr32_out    v14, v10, v29, \shift
        tr32_out    v14, v18, v30, \shift
        tr32_out    v14, v26, v31, \shift
        st1         {v28.1D - v31.1D}, [x9], #32
        tr32_out    v14, v3, v28, \shift
        tr32_out    v14, v11, v29, \shift
        tr32_out    v14, v19, v30, \shift
        tr32_out    v14, v27, v31, \shift
        st1         {v28.1D - v31.1D}, [x9], #32
        sub         x9, x9, #128
        transpose_16b_4x4 v0, v8, v16, v24, v28, v29, v30, v31
        transpose_16b_4x4 v1, v9, v17, v25, v28, v29, v30, v31
        transpose_16b_4x4 v2, v10, v18, v26, v28, v29, v30, v31
        transpose_16b_4x4 v3, v11, v19, v27, v28, v29, v30, v31
        ld1         {v31.1D}, [x9], #8
        ld1         {v23.1D}, [x9], #8
        ld1         {v15.1D}, [x9], #8
        ld1         {v7.1D},  [x9], #8
        ld1         {v30.1D}, [x9], #8
        ld1         {v22.1D}, [x9], #8
        ld1         {v14.1D}, [x9], #8
        ld1         {v6.1D},  [x9], #8
        ld1         {v29.1D}, [x9], #8
        ld1         {v21.1D}, [x9], #8
        ld1         {v13.1D}, [x9], #8
        ld1         {v5.1D},  [x9], #8
        ld1         {v28.1D}, [x9], #8
        ld1         {v20.1D}, [x9], #8
        ld1         {v12.1D}, [x9], #8
        ld1         {v4.1D},  [x9], #8
        sub         x9, x9, #128
        st1         {v24.1D - v27.1D}, [x9]
        transpose_16b_4x4 v4, v12, v20, v28, v24, v25, v26, v27
        transpose_16b_4x4 v5, v13, v21, v29, v24, v25, v26, v27
        transpose_16b_4x4 v6, v14, v22, v30, v24, v25, v26, v27
        transpose_16b_4x4 v7, v15, v23, v31, v24, v25, v26, v27
        ld1         {v24.1D - v27.1D}, [x9]
        \action
        sub         x6, x6, #256
        sub         x7, x7, #256
        add         x4, x4, #4
        cmp         x4, \limit
        b.lt        0b
        sub         x0, x0, x4, lsl #1
1:      cmp         x4, #32
        b.ge        2f
        \action
        add         x4, x4, #4
        b           1b
2:      mov         x8, x10
.endm

const trans_coeff, align=8
.quad 0x0000000000240053  // 36, 83
.quad 0x003200120059004b  // 89, 75, 50, 18
.quad 0x00500046005a0057  // 90, 87, 80, 70
.quad 0x001900090039002b  // 57, 43, 25, 9
.quad 0x00550058005a005a  // 88, 85, 90, 90
.quad 0x00430049004e0052  // 73, 67, 82, 78
.quad 0x0026002e0036003d  // 46, 38, 61, 54
.quad 0x0004000d0016001f  // 13, 04, 31, 22
endconst

function ff_hevc_transform_32x32_neon_8, export=1
        mov         x6, #32
        add         x7, x1, #4
        cmp         x1, #28
        csel        x1, x6, x7, gt
        mov         x2, x1
        movrel      x3, trans_coeff
        sub         x6, sp, #256
        mov         x7, #63
        bic         x6, x6, x7
        sub         x7, x6, #256
        sub         x8, x7, #2048
        sub         x9, x8, #256
        mov         x5, #64
        tr32_transform_func #7, write32_buffer, x2
        mov         x2, x0
        mov         x0, x8
        mov         x8, x2
        tr32_transform_func #12, write32_buffer, #32
        ret
endfunc

function ff_hevc_idct_4x4_dc_neon_8, export=1
        ldrsh       w2, [x0]
        dup         v8.8H, w2
        srshr       v8.8H, v8.8H, #1
        srshr       v8.8H, v8.8H, #6
        mov         v9.16B, v8.16B
        st1         {v8.2D, v9.2D}, [x0]
        ret
endfunc

function ff_hevc_idct_8x8_dc_neon_8, export=1
        ldrsh       w2, [x0]
        dup         v8.8H, w2
        srshr       v8.8H, v8.8H, #1
        srshr       v8.8H, v8.8H, #6
        mov         v9.16B,  v8.16B
        mov         v10.16B, v8.16B
        mov         v11.16B, v8.16B
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0]
        ret
endfunc

function ff_hevc_idct_16x16_dc_neon_8, export=1
        ldrsh       w2, [x0]
        dup         v8.8H, w2
        srshr       v8.8H, v8.8H, #1
        srshr       v8.8H, v8.8H, #6
        mov         v9.16B,  v8.16B
        mov         v10.16B, v8.16B
        mov         v11.16B, v8.16B
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0]
        ret
endfunc

function ff_hevc_idct_32x32_dc_neon_8, export=1
        ldrsh       w2, [x0]
        dup         v8.8H, w2
        srshr       v8.8H, v8.8H, #1
        srshr       v8.8H, v8.8H, #6
        mov         v9.16B,  v8.16B
        mov         v10.16B, v8.16B
        mov         v11.16B, v8.16B
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0], #64
        st1         {v8.2D - v11.2D}, [x0]
        ret
endfunc

function ff_hevc_transform_add_4x4_neon_8, export=1
        ld1         {v0.2D, v1.2D}, [x1]
        ld1         {v2.S}[0], [x0], x2
        ld1         {v2.S}[1], [x0], x2
        ld1         {v3.S}[0], [x0], x2
        ld1         {v3.S}[1], [x0], x2
        sub         x0, x0, x2, lsl #2
        uxtl        v8.8H, v2.8B
        uxtl        v9.8H, v3.8B
        sqadd       v0.8H, v0.8H, v8.8H
        sqadd       v1.8H, v1.8H, v9.8H
        sqxtun      v4.8B, v0.8H
        sqxtun      v5.8B, v1.8H
        st1         {v4.S}[0], [x0], x2
        st1         {v4.S}[1], [x0], x2
        st1         {v5.S}[0], [x0], x2
        st1         {v5.S}[1], [x0], x2
        ret
endfunc

function ff_hevc_transform_add_8x8_neon_8, export=1
        mov         x3, #8
1:      subs        x3, x3, #1
        ld1         {v0.2D}, [x1], #16
        ld1         {v8.1D}, [x0]
        uxtl        v8.8H, v8.8B
        sqadd       v0.8H, v0.8H, v8.8H
        sqxtun      v4.8B, v0.8H
        st1         {v4.1D}, [x0], x2
        b.ne        1b
        ret
endfunc

function ff_hevc_transform_add_16x16_neon_8, export=1
        mov         x3, #16
1:      subs        x3, x3, #1
        ld1         {v0.2D - v1.2D}, [x1], #32
        ld1         {v8.2D}, [x0]
        uxtl        v9.8H, v8.8B
        uxtl2       v10.8H, v8.16B
        sqadd       v0.8H, v0.8H, v9.8H
        sqadd       v1.8H, v1.8H, v10.8H
        sqxtun      v4.8B, v0.8H
        sqxtun2     v4.16B, v1.8H
        st1         {v4.2D}, [x0], x2
        b.ne        1b
        ret
endfunc

function ff_hevc_transform_add_32x32_neon_8, export=1
        mov         x3, #32
1:      subs        x3, x3, #1
        ld1         {v0.2D - v3.2D}, [x1], #64
        ld1         {v8.2D, v9.2D}, [x0]
        uxtl        v10.8H, v8.8B
        uxtl2       v11.8H, v8.16B
        uxtl        v12.8H, v9.8B
        uxtl2       v13.8H, v9.16B
        sqadd       v0.8H, v0.8H, v10.8H
        sqadd       v1.8H, v1.8H, v11.8H
        sqadd       v2.8H, v2.8H, v12.8H
        sqadd       v3.8H, v3.8H, v13.8H
        sqxtun      v4.8B, v0.8H
        sqxtun2     v4.16B, v1.8H
        sqxtun      v5.8B, v2.8H
        sqxtun2     v5.16B, v3.8H
        st1         {v4.2D, v5.2D}, [x0], x2
        b.ne        1b
        ret
endfunc

function ff_hevc_transform_4x4_neon_8, export=1
        ld1         {v28.2D - v29.2D}, [x0]
        ldr         w2, =0x00240053
        mov         v0.S[0], w2
        tr4_shift   v28, v29, #7 //3210->3120
        zip1        v26.8H, v28.8H, v29.8H
        zip2        v27.8H, v28.8H, v29.8H
        zip1        v28.4S, v26.4S, v27.4S
        zip2        v29.4S, v26.4S, v27.4S
        tr4_shift   v28, v29, #12 //3210->3120
        zip1        v26.8H, v28.8H, v29.8H
        zip2        v27.8H, v28.8H, v29.8H
        zip1        v28.4S, v26.4S, v27.4S
        zip2        v29.4S, v26.4S, v27.4S
        st1         {v28.2D - v29.2D}, [x0]
        ret	
endfunc

function ff_hevc_transform_luma_4x4_neon_8, export=1
        ld1         {v28.2D - v29.2D}, [x0]
        ldr         x1, =0x0037001d004a
        mov         v1.D[0], x1
        uxtl        v0.4S, v1.4H
        tr4_luma_shift  v28, v29, #7
        zip1        v26.8H, v28.8H, v29.8H
        zip2        v27.8H, v28.8H, v29.8H
        zip1        v28.4S, v26.4S, v27.4S
        zip2        v29.4S, v26.4S, v27.4S
        tr4_luma_shift	v28, v29, #12
        zip1        v26.8H, v28.8H, v29.8H
        zip2        v27.8H, v28.8H, v29.8H
        zip1        v28.4S, v26.4S, v27.4S
        zip2        v29.4S, v26.4S, v27.4S
        st1         {v28.2D - v29.2D}, [x0]
        ret			
endfunc

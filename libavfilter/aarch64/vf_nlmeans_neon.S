/*
 * Copyright (c) 2018 Clément Bœsch <u pkh me>
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

// acc_sum_store(ABCD) = {X+A, X+A+B, X+A+B+C, X+A+B+C+D}
.macro acc_sum_store x, xb
        dup             v24.4S, v24.S[3]                                // ...X -> XXXX
        ext             v25.16B, v26.16B, \xb, #12                      // ext(0000,ABCD,12)=0ABC
        add             v24.4S, v24.4S, \x                              // XXXX+ABCD={X+A,X+B,X+C,X+D}
        add             v24.4S, v24.4S, v25.4S                          // {X+A,X+B+A,X+C+B,X+D+C}       (+0ABC)
        ext             v25.16B, v26.16B, v25.16B, #12                  // ext(0000,0ABC,12)=00AB
        add             v24.4S, v24.4S, v25.4S                          // {X+A,X+B+A,X+C+B+A,X+D+C+B}   (+00AB)
        ext             v25.16B, v26.16B, v25.16B, #12                  // ext(0000,00AB,12)=000A
        add             v24.4S, v24.4S, v25.4S                          // {X+A,X+B+A,X+C+B+A,X+D+C+B+A} (+000A)
        st1             {v24.4S}, [x0], #16                             // write 4x32-bit final values
.endm

function ff_compute_safe_ssd_integral_image_neon, export=1
        movi            v26.4S, #0                                      // used as zero for the "rotations" in acc_sum_store
        sub             x3, x3, w6, UXTW                                // s1 padding (s1_linesize - w)
        sub             x5, x5, w6, UXTW                                // s2 padding (s2_linesize - w)
        sub             x9, x0, w1, UXTW #2                             // dst_top
        sub             x1, x1, w6, UXTW                                // dst padding (dst_linesize_32 - w)
        lsl             x1, x1, #2                                      // dst padding expressed in bytes
1:      mov             w10, w6                                         // width copy for each line
        sub             x0, x0, #16                                     // beginning of the dst line minus 4 sums
        sub             x8, x9, #4                                      // dst_top-1
        ld1             {v24.4S}, [x0], #16                             // load ...X (contextual last sums)
2:      ld1             {v0.16B}, [x2], #16                             // s1[x + 0..15]
        ld1             {v1.16B}, [x4], #16                             // s2[x + 0..15]
        ld1             {v16.4S,v17.4S}, [x8], #32                      // dst_top[x + 0..7 - 1]
        usubl           v2.8H, v0.8B,  v1.8B                            // d[x + 0..7]  = s1[x + 0..7]  - s2[x + 0..7]
        usubl2          v3.8H, v0.16B, v1.16B                           // d[x + 8..15] = s1[x + 8..15] - s2[x + 8..15]
        ld1             {v18.4S,v19.4S}, [x8], #32                      // dst_top[x + 8..15 - 1]
        smull           v4.4S, v2.4H, v2.4H                             // d[x + 0..3]^2
        smull2          v5.4S, v2.8H, v2.8H                             // d[x + 4..7]^2
        ld1             {v20.4S,v21.4S}, [x9], #32                      // dst_top[x + 0..7]
        smull           v6.4S, v3.4H, v3.4H                             // d[x + 8..11]^2
        smull2          v7.4S, v3.8H, v3.8H                             // d[x + 12..15]^2
        ld1             {v22.4S,v23.4S}, [x9], #32                      // dst_top[x + 8..15]
        sub             v0.4S, v20.4S, v16.4S                           // dst_top[x + 0..3] - dst_top[x + 0..3 - 1]
        sub             v1.4S, v21.4S, v17.4S                           // dst_top[x + 4..7] - dst_top[x + 4..7 - 1]
        add             v0.4S, v0.4S, v4.4S                             // + d[x + 0..3]^2
        add             v1.4S, v1.4S, v5.4S                             // + d[x + 4..7]^2
        sub             v2.4S, v22.4S, v18.4S                           // dst_top[x +  8..11] - dst_top[x +  8..11 - 1]
        sub             v3.4S, v23.4S, v19.4S                           // dst_top[x + 12..15] - dst_top[x + 12..15 - 1]
        add             v2.4S, v2.4S, v6.4S                             // + d[x +  8..11]^2
        add             v3.4S, v3.4S, v7.4S                             // + d[x + 12..15]^2
        acc_sum_store   v0.4S, v0.16B                                   // accumulate and store dst[ 0..3]
        acc_sum_store   v1.4S, v1.16B                                   // accumulate and store dst[ 4..7]
        acc_sum_store   v2.4S, v2.16B                                   // accumulate and store dst[ 8..11]
        acc_sum_store   v3.4S, v3.16B                                   // accumulate and store dst[12..15]
        subs            w10, w10, #16                                   // width dec
        b.ne            2b                                              // loop til next line
        add             x2, x2, x3                                      // skip to next line (s1)
        add             x4, x4, x5                                      // skip to next line (s2)
        add             x0, x0, x1                                      // skip to next line (dst)
        add             x9, x9, x1                                      // skip to next line (dst_top)
        subs            w7, w7, #1                                      // height dec
        b.ne            1b
        ret
endfunc

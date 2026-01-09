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
        dup             v24.4s, v24.s[3]                                // ...X -> XXXX
        ext             v25.16b, v26.16b, \xb, #12                      // ext(0000,ABCD,12)=0ABC
        add             v24.4s, v24.4s, \x                              // XXXX+ABCD={X+A,X+B,X+C,X+D}
        add             v24.4s, v24.4s, v25.4s                          // {X+A,X+B+A,X+C+B,X+D+C}       (+0ABC)
        ext             v25.16b, v26.16b, v25.16b, #12                  // ext(0000,0ABC,12)=00AB
        add             v24.4s, v24.4s, v25.4s                          // {X+A,X+B+A,X+C+B+A,X+D+C+B}   (+00AB)
        ext             v25.16b, v26.16b, v25.16b, #12                  // ext(0000,00AB,12)=000A
        add             v24.4s, v24.4s, v25.4s                          // {X+A,X+B+A,X+C+B+A,X+D+C+B+A} (+000A)
        st1             {v24.4s}, [x0], #16                             // write 4x32-bit final values
.endm

function ff_compute_safe_ssd_integral_image_neon, export=1
        movi            v26.4s, #0                                      // used as zero for the "rotations" in acc_sum_store
        sub             x3, x3, w6, uxtw                                // s1 padding (s1_linesize - w)
        sub             x5, x5, w6, uxtw                                // s2 padding (s2_linesize - w)
        sub             x9, x0, w1, uxtw #2                             // dst_top
        sub             x1, x1, w6, uxtw                                // dst padding (dst_linesize_32 - w)
        lsl             x1, x1, #2                                      // dst padding expressed in bytes
1:      mov             w10, w6                                         // width copy for each line
        sub             x0, x0, #16                                     // beginning of the dst line minus 4 sums
        sub             x8, x9, #4                                      // dst_top-1
        ld1             {v24.4s}, [x0], #16                             // load ...X (contextual last sums)
2:      ld1             {v0.16b}, [x2], #16                             // s1[x + 0..15]
        ld1             {v1.16b}, [x4], #16                             // s2[x + 0..15]
        ld1             {v16.4s,v17.4s}, [x8], #32                      // dst_top[x + 0..7 - 1]
        usubl           v2.8h, v0.8b,  v1.8b                            // d[x + 0..7]  = s1[x + 0..7]  - s2[x + 0..7]
        usubl2          v3.8h, v0.16b, v1.16b                           // d[x + 8..15] = s1[x + 8..15] - s2[x + 8..15]
        ld1             {v18.4s,v19.4s}, [x8], #32                      // dst_top[x + 8..15 - 1]
        smull           v4.4s, v2.4h, v2.4h                             // d[x + 0..3]^2
        smull2          v5.4s, v2.8h, v2.8h                             // d[x + 4..7]^2
        ld1             {v20.4s,v21.4s}, [x9], #32                      // dst_top[x + 0..7]
        smull           v6.4s, v3.4h, v3.4h                             // d[x + 8..11]^2
        smull2          v7.4s, v3.8h, v3.8h                             // d[x + 12..15]^2
        ld1             {v22.4s,v23.4s}, [x9], #32                      // dst_top[x + 8..15]
        sub             v0.4s, v20.4s, v16.4s                           // dst_top[x + 0..3] - dst_top[x + 0..3 - 1]
        sub             v1.4s, v21.4s, v17.4s                           // dst_top[x + 4..7] - dst_top[x + 4..7 - 1]
        add             v0.4s, v0.4s, v4.4s                             // + d[x + 0..3]^2
        add             v1.4s, v1.4s, v5.4s                             // + d[x + 4..7]^2
        sub             v2.4s, v22.4s, v18.4s                           // dst_top[x +  8..11] - dst_top[x +  8..11 - 1]
        sub             v3.4s, v23.4s, v19.4s                           // dst_top[x + 12..15] - dst_top[x + 12..15 - 1]
        add             v2.4s, v2.4s, v6.4s                             // + d[x +  8..11]^2
        add             v3.4s, v3.4s, v7.4s                             // + d[x + 12..15]^2
        acc_sum_store   v0.4s, v0.16b                                   // accumulate and store dst[ 0..3]
        acc_sum_store   v1.4s, v1.16b                                   // accumulate and store dst[ 4..7]
        acc_sum_store   v2.4s, v2.16b                                   // accumulate and store dst[ 8..11]
        acc_sum_store   v3.4s, v3.16b                                   // accumulate and store dst[12..15]
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

function ff_compute_weights_line_neon, export=1
        // x0 = iia, x1 = iib, x2 = iid, x3 = iie
        // x4 = src, x5 = total_weight, x6 = sum, x7 = weight_lut
        // stack: [sp+0] = max_meaningful_diff, [sp+8] = startx, [sp+16] = endx

        ldr             w13, [sp, #0]                  // max_meaningful_diff
        ldr             w9,  [sp, #8]                   // startx
        ldr             w10, [sp, #16]                  // endx

        cmp             w9, w10
        b.ge            9f                              // if startx >= endx return

        // Offset pointers
        lsl             x11, x9, #2                     // startx * 4 (for uint32/float)
        add             x0, x0, x11                     // iia += startx
        add             x1, x1, x11                     // iib += startx
        add             x2, x2, x11                     // iid += startx
        add             x3, x3, x11                     // iie += startx
        add             x5, x5, x11                     // total_weight += startx
        add             x6, x6, x11                     // sum += startx

        // src is uint8, so offset is just startx
        add             x4, x4, x9                      // src += startx

        dup             v7.4s, w13                      // v7 = max_meaningful_diff (for vector ops)

        sub             w10, w10, w9                    // count = endx - startx

1:      // Main loop
        cmp             w10, #4
        b.lt            2f                              // Handle leftovers

        // Load integral image values
        ld1             {v0.4s}, [x0], #16              // iia
        ld1             {v1.4s}, [x1], #16              // iib
        ld1             {v2.4s}, [x2], #16              // iid
        ld1             {v3.4s}, [x3], #16              // iie

        // diff = a - b + e - d = e - d - b + a
        sub             v0.4s, v0.4s, v1.4s             // v0 = a - b
        sub             v3.4s, v3.4s, v2.4s             // v3 = e - d
        add             v3.4s, v3.4s, v0.4s             // v3 = diff (a - b + e - d)

        // min(diff, max)
        umin            v3.4s, v3.4s, v7.4s

        // Schedule independent loads early
        ld1             {v0.4s}, [x5]                   // v0 = total_weight
        ld1             {v1.s}[0], [x4], #4             // v1 = src pixels (low 4 bytes)
        ld1             {v2.4s}, [x6]                   // v2 = sum

        // Move to scalar registers to address lut
        mov             w8,  v3.s[0]
        mov             w9,  v3.s[1]
        mov             w11, v3.s[2]
        mov             w12, v3.s[3]

        // Load 4 float weights using scalar registers
        // Interleave with src conversion to hide latency
        ldr             s3, [x7, w8, uxtw #2]           // w0 -> v3.s[0] (v3 is now free)
        ldr             s4, [x7, w9, uxtw #2]           // w1 -> v4.s[0]
        ldr             s5, [x7, w11, uxtw #2]          // w2 -> v5.s[0]
        ldr             s6, [x7, w12, uxtw #2]          // w3 -> v6.s[0]

        // Convert src pixels to float (independent of weights)
        uxtl            v1.8h, v1.8b
        uxtl            v1.4s, v1.4h
        ucvtf           v1.4s, v1.4s

        // Merge weights into v3.4s
        trn1            v3.2s, v3.2s, v4.2s             // v3 = [w0, w1, ?, ?]
        trn1            v5.2s, v5.2s, v6.2s             // v5 = [w2, w3, ?, ?]
        trn1            v3.2d, v3.2d, v5.2d             // v3 = [w0, w1, w2, w3]

        // Update total_weight and sum
        fadd            v0.4s, v0.4s, v3.4s             // total_weight += weight
        fmla            v2.4s, v1.4s, v3.4s             // sum += src * weight

        // Store back
        st1             {v0.4s}, [x5], #16
        st1             {v2.4s}, [x6], #16

        sub             w10, w10, #4
        b               1b

2:      // Leftovers
        cmp             w10, #0
        b.le            9f

        // Single pixel handling
        ldr             w8,  [x0], #4                   // iia (reuse w8)
        ldr             w9,  [x1], #4                   // iib (reuse w9)
        ldr             w11, [x2], #4                   // iid (reuse w11)
        ldr             w12, [x3], #4                   // iie (reuse w12)

        sub             w12, w12, w11
        sub             w12, w12, w9
        add             w12, w12, w8

        // min (unsigned comparison) - use preloaded w13
        cmp             w12, w13
        csel            w12, w12, w13, ls               // unsigned lower or same

        // Load weight
        ldr             s0, [x7, w12, uxtw #2]

        // Load src
        ldrb            w8, [x4], #1                    // src (reuse w8)
        ucvtf           s1, w8

        // Load acc
        ldr             s2, [x5]
        ldr             s3, [x6]

        fadd            s2, s2, s0
        fmadd           s3, s1, s0, s3

        str             s2, [x5], #4
        str             s3, [x6], #4

        sub             w10, w10, #1
        b               2b

9:      ret
endfunc

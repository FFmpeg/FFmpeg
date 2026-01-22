/*
 * ARM NEON optimised dequant functions for HEVC decoding
 *
 * Copyright (c) 2026 FFmpeg contributors
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

// HEVC dequant for 8-bit depth
//
// Algorithm (from dsp_template.c):
//   shift  = 15 - BIT_DEPTH - log2_size
//   offset = 1 << (shift - 1)
//   output = (input + offset) >> shift
//
// This is equivalent to: output = ROUND(input >> shift)
// NEON srshr (Signed Rounding Shift Right) does exactly this in one instruction!
//
// For 8-bit: shift = 15 - 8 - log2_size = 7 - log2_size
//
// Block size | log2_size | shift | operation
// 4x4        | 2         | 5     | srshr #5
// 8x8        | 3         | 4     | srshr #4
// 16x16      | 4         | 3     | srshr #3
// 32x32      | 5         | 2     | srshr #2

// void ff_hevc_dequant_4x4_8_neon(int16_t *coeffs)
// 4x4 = 16 coeffs, shift=5
function ff_hevc_dequant_4x4_8_neon, export=1
        ldp             q0, q1, [x0]            // load 16 int16_t (32 bytes)
        srshr           v0.8h, v0.8h, #5        // rounding shift right by 5
        srshr           v1.8h, v1.8h, #5
        stp             q0, q1, [x0]            // store
        ret
endfunc

// void ff_hevc_dequant_8x8_8_neon(int16_t *coeffs)
// 8x8 = 64 coeffs, shift=4
// Fully unrolled - no loop needed for 64 coeffs
function ff_hevc_dequant_8x8_8_neon, export=1
        ld1             {v0.16b-v3.16b}, [x0], #64
        ld1             {v4.16b-v7.16b}, [x0]
        sub             x0, x0, #64
        srshr           v0.8h, v0.8h, #4
        srshr           v1.8h, v1.8h, #4
        srshr           v2.8h, v2.8h, #4
        srshr           v3.8h, v3.8h, #4
        srshr           v4.8h, v4.8h, #4
        srshr           v5.8h, v5.8h, #4
        srshr           v6.8h, v6.8h, #4
        srshr           v7.8h, v7.8h, #4
        st1             {v0.16b-v3.16b}, [x0], #64
        st1             {v4.16b-v7.16b}, [x0]
        ret
endfunc

// void ff_hevc_dequant_16x16_8_neon(int16_t *coeffs)
// 16x16 = 256 coeffs, shift=3
// Pipelined implementation: interleave load/compute/store to hide memory latency
// Uses .irp macro to unroll 4 iterations, processing 64 coeffs per iteration
// x0 = load pointer, x1 = store pointer (both advance through the buffer)
function ff_hevc_dequant_16x16_8_neon, export=1
        mov             x1, x0
        ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x0], #64
.irp i, 0, 1, 2, 3
        srshr           v0.8h, v0.8h, #3
        srshr           v1.8h, v1.8h, #3
        ld1             {v4.16b, v5.16b, v6.16b, v7.16b}, [x0], #64
        srshr           v2.8h, v2.8h, #3
        srshr           v3.8h, v3.8h, #3
        srshr           v4.8h, v4.8h, #3
        srshr           v5.8h, v5.8h, #3
        st1             {v0.16b - v3.16b}, [x1], #64
        srshr           v6.8h, v6.8h, #3
.if \i < 3
        ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x0], #64
.endif
        srshr           v7.8h, v7.8h, #3
        st1             {v4.16b - v7.16b}, [x1], #64
.endr
        ret
endfunc

// void ff_hevc_dequant_32x32_8_neon(int16_t *coeffs)
// 32x32 = 1024 coeffs, shift=2
// Process 128 coeffs per iteration (8 iterations)
// Using all available NEON registers for maximum throughput
// AAPCS64: v0-v7 and v16-v31 are volatile (caller-saved)
// We use v0-v7 and v16-v23 to avoid touching callee-saved v8-v15
function ff_hevc_dequant_32x32_8_neon, export=1
        mov             x2, #8                     // loop 8 times (128 coeffs each)
1:
        // Group A: q0-q3 (64 bytes / 32 coeffs)
        ldp             q0, q1, [x0]
        ldp             q2, q3, [x0, #32]
        // Group B: q4-q7 (64 bytes / 32 coeffs)
        ldp             q4, q5, [x0, #64]
        ldp             q6, q7, [x0, #96]
        subs            x2, x2, #1                 // Decrement loop counter early for better pipelining

        // Calc Group A (shift right with rounding)
        srshr           v0.8h, v0.8h, #2
        srshr           v1.8h, v1.8h, #2
        srshr           v2.8h, v2.8h, #2
        srshr           v3.8h, v3.8h, #2

        // Group C: q16-q19 (64 bytes / 32 coeffs)
        // Load into volatile high registers to maximize pipeline usage
        ldp             q16, q17, [x0, #128]
        ldp             q18, q19, [x0, #160]

        // Calc Group B
        srshr           v4.8h, v4.8h, #2
        srshr           v5.8h, v5.8h, #2
        srshr           v6.8h, v6.8h, #2
        srshr           v7.8h, v7.8h, #2

        // Store Group A (Write back results to memory)
        stp             q0, q1, [x0]
        stp             q2, q3, [x0, #32]

        // Group D: q20-q23 (64 bytes / 32 coeffs)
        ldp             q20, q21, [x0, #192]
        ldp             q22, q23, [x0, #224]

        // Calc Group C
        srshr           v16.8h, v16.8h, #2
        srshr           v17.8h, v17.8h, #2
        srshr           v18.8h, v18.8h, #2
        srshr           v19.8h, v19.8h, #2

        // Store Group B
        stp             q4, q5, [x0, #64]
        stp             q6, q7, [x0, #96]

        // Calc Group D
        srshr           v20.8h, v20.8h, #2
        srshr           v21.8h, v21.8h, #2
        srshr           v22.8h, v22.8h, #2
        srshr           v23.8h, v23.8h, #2

        // Store Group C
        stp             q16, q17, [x0, #128]
        stp             q18, q19, [x0, #160]

        // Store Group D
        stp             q20, q21, [x0, #192]
        stp             q22, q23, [x0, #224]

        add             x0, x0, #256           // Advance pointer by 128 coeffs (256 bytes)
        b.ne            1b
        ret
endfunc

// --------------------------------------------------------------------------
// HEVC dequant for 10-bit depth
//
// For 10-bit: shift = 15 - 10 - log2_size = 5 - log2_size
//
// Block size | log2_size | shift | operation
// 4x4        | 2         | 3     | srshr #3
// 8x8        | 3         | 2     | srshr #2
// 16x16      | 4         | 1     | srshr #1
// 32x32      | 5         | 0     | no-op (identity)
// --------------------------------------------------------------------------

// void ff_hevc_dequant_4x4_10_neon(int16_t *coeffs)
// 4x4 = 16 coeffs, shift=3
function ff_hevc_dequant_4x4_10_neon, export=1
        ldp             q0, q1, [x0]
        srshr           v0.8h, v0.8h, #3
        srshr           v1.8h, v1.8h, #3
        stp             q0, q1, [x0]
        ret
endfunc

// void ff_hevc_dequant_8x8_10_neon(int16_t *coeffs)
// 8x8 = 64 coeffs, shift=2
// Fully unrolled - no loop needed for 64 coeffs
function ff_hevc_dequant_8x8_10_neon, export=1
        ld1             {v0.16b-v3.16b}, [x0], #64
        ld1             {v4.16b-v7.16b}, [x0]
        sub             x0, x0, #64
        srshr           v0.8h, v0.8h, #2
        srshr           v1.8h, v1.8h, #2
        srshr           v2.8h, v2.8h, #2
        srshr           v3.8h, v3.8h, #2
        srshr           v4.8h, v4.8h, #2
        srshr           v5.8h, v5.8h, #2
        srshr           v6.8h, v6.8h, #2
        srshr           v7.8h, v7.8h, #2
        st1             {v0.16b-v3.16b}, [x0], #64
        st1             {v4.16b-v7.16b}, [x0]
        ret
endfunc

// void ff_hevc_dequant_16x16_10_neon(int16_t *coeffs)
// 16x16 = 256 coeffs, shift=1
// Pipelined implementation: interleave load/compute/store to hide memory latency
// Uses .irp macro to unroll 4 iterations, processing 64 coeffs per iteration
// x0 = load pointer, x1 = store pointer (both advance through the buffer)
function ff_hevc_dequant_16x16_10_neon, export=1
        mov             x1, x0
        ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x0], #64
.irp i, 0, 1, 2, 3
        srshr           v0.8h, v0.8h, #1
        srshr           v1.8h, v1.8h, #1
        ld1             {v4.16b, v5.16b, v6.16b, v7.16b}, [x0], #64
        srshr           v2.8h, v2.8h, #1
        srshr           v3.8h, v3.8h, #1
        srshr           v4.8h, v4.8h, #1
        srshr           v5.8h, v5.8h, #1
        st1             {v0.16b - v3.16b}, [x1], #64
        srshr           v6.8h, v6.8h, #1
.if \i < 3
        ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x0], #64
.endif
        srshr           v7.8h, v7.8h, #1
        st1             {v4.16b - v7.16b}, [x1], #64
.endr
        ret
endfunc

// void ff_hevc_dequant_32x32_10_neon(int16_t *coeffs)
// 32x32 = 1024 coeffs, shift=0
// When shift=0: output = (input + 0) >> 0 = input (identity transform)
// No operation needed - just return immediately
function ff_hevc_dequant_32x32_10_neon, export=1
        ret
endfunc

// --------------------------------------------------------------------------
// HEVC dequant for 12-bit depth
//
// For 12-bit: shift = 15 - 12 - log2_size = 3 - log2_size
//
// Block size | log2_size | shift | operation
// 4x4        | 2         | 1     | srshr #1 (shift right)
// 8x8        | 3         | 0     | no-op (identity)
// 16x16      | 4         | -1    | shl #1 (shift left)
// 32x32      | 5         | -2    | shl #2 (shift left)
// --------------------------------------------------------------------------

// void ff_hevc_dequant_4x4_12_neon(int16_t *coeffs)
// 4x4 = 16 coeffs, shift=1
function ff_hevc_dequant_4x4_12_neon, export=1
        ldp             q0, q1, [x0]
        srshr           v0.8h, v0.8h, #1
        srshr           v1.8h, v1.8h, #1
        stp             q0, q1, [x0]
        ret
endfunc

// void ff_hevc_dequant_8x8_12_neon(int16_t *coeffs)
// 8x8 = 64 coeffs, shift=0
// When shift=0: output = input (identity transform)
// No operation needed - just return immediately
function ff_hevc_dequant_8x8_12_neon, export=1
        ret
endfunc

// void ff_hevc_dequant_16x16_12_neon(int16_t *coeffs)
// 16x16 = 256 coeffs, shift=-1 (left shift by 1)
// Pipelined implementation: interleave load/compute/store to hide memory latency
// Uses .irp macro to unroll 4 iterations, processing 64 coeffs per iteration
// x0 = load pointer, x1 = store pointer (both advance through the buffer)
function ff_hevc_dequant_16x16_12_neon, export=1
        mov             x1, x0
        ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x0], #64
.irp i, 0, 1, 2, 3
        shl             v0.8h, v0.8h, #1
        shl             v1.8h, v1.8h, #1
        ld1             {v4.16b, v5.16b, v6.16b, v7.16b}, [x0], #64
        shl             v2.8h, v2.8h, #1
        shl             v3.8h, v3.8h, #1
        shl             v4.8h, v4.8h, #1
        shl             v5.8h, v5.8h, #1
        st1             {v0.16b - v3.16b}, [x1], #64
        shl             v6.8h, v6.8h, #1
.if \i < 3
        ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x0], #64
.endif
        shl             v7.8h, v7.8h, #1
        st1             {v4.16b - v7.16b}, [x1], #64
.endr
        ret
endfunc

// void ff_hevc_dequant_32x32_12_neon(int16_t *coeffs)
// 32x32 = 1024 coeffs, shift=-2 (left shift by 2)
// Process 128 coeffs per iteration (8 iterations)
// Using pipelined load/compute/store for better performance
function ff_hevc_dequant_32x32_12_neon, export=1
        mov             x2, #8
1:
        // Group A: q0-q3 (64 bytes / 32 coeffs)
        ldp             q0, q1, [x0]
        ldp             q2, q3, [x0, #32]
        // Group B: q4-q7 (64 bytes / 32 coeffs)
        ldp             q4, q5, [x0, #64]
        ldp             q6, q7, [x0, #96]
        subs            x2, x2, #1                 // Decrement loop counter early for better pipelining

        // Calc Group A (shift left by 2)
        shl             v0.8h, v0.8h, #2
        shl             v1.8h, v1.8h, #2
        shl             v2.8h, v2.8h, #2
        shl             v3.8h, v3.8h, #2

        // Group C: q16-q19 (64 bytes / 32 coeffs)
        ldp             q16, q17, [x0, #128]
        ldp             q18, q19, [x0, #160]

        // Calc Group B
        shl             v4.8h, v4.8h, #2
        shl             v5.8h, v5.8h, #2
        shl             v6.8h, v6.8h, #2
        shl             v7.8h, v7.8h, #2

        // Store Group A
        stp             q0, q1, [x0]
        stp             q2, q3, [x0, #32]

        // Group D: q20-q23 (64 bytes / 32 coeffs)
        ldp             q20, q21, [x0, #192]
        ldp             q22, q23, [x0, #224]

        // Calc Group C
        shl             v16.8h, v16.8h, #2
        shl             v17.8h, v17.8h, #2
        shl             v18.8h, v18.8h, #2
        shl             v19.8h, v19.8h, #2

        // Store Group B
        stp             q4, q5, [x0, #64]
        stp             q6, q7, [x0, #96]

        // Calc Group D
        shl             v20.8h, v20.8h, #2
        shl             v21.8h, v21.8h, #2
        shl             v22.8h, v22.8h, #2
        shl             v23.8h, v23.8h, #2

        // Store Group C
        stp             q16, q17, [x0, #128]
        stp             q18, q19, [x0, #160]

        // Store Group D
        stp             q20, q21, [x0, #192]
        stp             q22, q23, [x0, #224]

        add             x0, x0, #256
        b.ne            1b
        ret
endfunc

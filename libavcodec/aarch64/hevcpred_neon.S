/*
 * HEVC Intra Prediction NEON optimizations
 *
 * Copyright (c) 2026 Jun Zhao <barryjzhao@tencent.com>
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

/* HEVC Intra Prediction NEON functions
 *
 * Internal NEON function signatures — the C dispatch wrappers in
 * hevcpred_init_aarch64.c handle log2_size-based dispatch, so these
 * per-size functions do not take log2_size themselves:
 *
 * pred_dc_NxN:       void (uint8_t *src, const uint8_t *top,
 *                          const uint8_t *left, ptrdiff_t stride, int c_idx)
 * pred_planar_NxN:   void (uint8_t *src, const uint8_t *top,
 *                          const uint8_t *left, ptrdiff_t stride)
 * pred_angular_*_NxN: void (uint8_t *src, const uint8_t *top,
 *                           const uint8_t *left, ptrdiff_t stride,
 *                           int c_idx, int mode)
 *
 * Mode 10 and 26 accept log2_size since they share one entry point per mode.
 */

// =============================================================================
// DC Prediction
// =============================================================================

/*
 * DC prediction algorithm:
 * 1. dc = sum(top[0..size-1]) + sum(left[0..size-1]) + size
 * 2. dc >>= (log2_size + 1)
 * 3. Fill block with dc value
 * 4. If c_idx == 0 && size < 32: smooth edges
 *    - POS(0,0) = (left[0] + 2*dc + top[0] + 2) >> 2
 *    - First row: (top[x] + 3*dc + 2) >> 2
 *    - First col: (left[y] + 3*dc + 2) >> 2
*/

// -----------------------------------------------------------------------------
// pred_dc_4x4_8: DC prediction
// Arguments:
// x0: src
// x1: top
// x2: left
// x3: stride
// w4: c_idx
// -----------------------------------------------------------------------------
function ff_hevc_pred_dc_4x4_8_neon, export=1
        // Load top[0..3] and left[0..3]
        ldr             s0, [x1]                // top[0..3]
        ldr             s1, [x2]                // left[0..3]

        // Sum using NEON
        uaddlv          h2, v0.8b               // sum top (only 4 valid bytes)
        uaddlv          h3, v1.8b               // sum left (only 4 valid bytes)
        add             v2.4h, v2.4h, v3.4h     // total sum

        // Add rounding and shift by 3 (urshr = unsigned rounding shift right)
        add             x5, x0, x3, lsl #1      // row 2 address (early for str)
        urshr           v2.4h, v2.4h, #3        // (sum + 4) >> 3
        dup             v2.8b, v2.b[0]          // broadcast dc

        // Store 4 rows
        str             s2, [x0]
        str             s2, [x0, x3]
        str             s2, [x5]
        str             s2, [x5, x3]

        // Edge smoothing for luma only
        cbnz            w4, 2f

        // Compute 3*dc in NEON domain (16-bit)
        uxtl            v3.8h, v2.8b            // widen dc to 16-bit
        add             v6.8h, v3.8h, v3.8h     // 2*dc
        add             v3.8h, v6.8h, v3.8h     // 3*dc

        // Widen top and left to 16-bit
        uxtl            v4.8h, v0.8b            // top[0..3] widened
        uxtl            v5.8h, v1.8b            // left[0..3] widened

        // Corner: (top[0] + left[0] + 2*dc + 2) >> 2
        // First row: (top[x] + 3*dc + 2) >> 2
        // First column: (left[y] + 3*dc + 2) >> 2
        add             v7.4h, v4.4h, v5.4h     // corner: top[x] + left[x] (only lane 0 matters)
        add             v4.8h, v4.8h, v3.8h     // first row: top[x] + 3*dc
        add             v5.8h, v5.8h, v3.8h     // first column: left[y] + 3*dc
        add             v7.4h, v7.4h, v6.4h     // corner: + 2*dc
        rshrn           v4.8b, v4.8h, #2        // first row: (x + 2) >> 2
        rshrn           v5.8b, v5.8h, #2        // first column: (x + 2) >> 2
        rshrn           v7.8b, v7.8h, #2        // corner: (x + 2) >> 2

        // Overwrite corner byte in row result
        ins             v4.b[0], v7.b[0]

        // Store smoothed first row
        str             s4, [x0]

        // Store smoothed column for y=1..3
        add             x5, x0, x3
        add             x6, x0, x3, lsl #1
        add             x7, x5, x3, lsl #1
        st1             {v5.b}[1], [x5]
        st1             {v5.b}[2], [x6]
        st1             {v5.b}[3], [x7]

2:      ret
endfunc

// -----------------------------------------------------------------------------
// pred_dc_8x8_8: DC prediction
// Arguments:
// x0: src
// x1: top
// x2: left
// x3: stride
// w4: c_idx
// -----------------------------------------------------------------------------
function ff_hevc_pred_dc_8x8_8_neon, export=1
        // Load top[0..7] and left[0..7]
        ldr             d0, [x1]                // top[0..7]
        ldr             d1, [x2]                // left[0..7]

        // Sum all pixels
        uaddlv          h2, v0.8b               // sum top
        uaddlv          h3, v1.8b               // sum left
        add             v2.4h, v2.4h, v3.4h     // total sum

        // Add rounding and shift by 4
        urshr           v2.4h, v2.4h, #4        // (sum + 8) >> 4
        dup             v2.8b, v2.b[0]          // broadcast dc

        // Check if edge smoothing needed (luma only)
        cbnz            w4, 2f

        // === Luma path: fill + edge smoothing combined ===

        // Compute 3*dc in NEON domain (16-bit)
        uxtl            v3.8h, v2.8b            // widen dc to 16-bit
        add             v6.8h, v3.8h, v3.8h     // 2*dc
        add             v3.8h, v6.8h, v3.8h     // 3*dc

        // Widen top and left to 16-bit
        uxtl            v4.8h, v0.8b
        uxtl            v5.8h, v1.8b

        // Corner: (top[0] + left[0] + 2*dc + 2) >> 2
        // Smoothed first row: (top[x] + 3*dc + 2) >> 2
        // Smoothed column: (left[y] + 3*dc + 2) >> 2
        add             v7.4h, v4.4h, v5.4h     // corner: top[x] + left[x] (only lane 0 matters)
        add             v4.8h, v4.8h, v3.8h     // first row: top[x] + 3*dc
        add             v5.8h, v5.8h, v3.8h     // column: left[y] + 3*dc
        add             v7.4h, v7.4h, v6.4h     // corner: + 2*dc
        rshrn           v4.8b, v4.8h, #2        // first row: (x + 2) >> 2
        rshrn           v5.8b, v5.8h, #2        // column: (x + 2) >> 2
        rshrn           v7.8b, v7.8h, #2        // corner: (x + 2) >> 2
        // Overwrite corner byte
        ins             v4.b[0], v7.b[0]

        // Store row 0 (smoothed)
        str             d4, [x0]

        // Store DC fill for rows 1-7 with pre-computed addresses
        add             x15, x0, x3, lsl #1
        str             d2, [x0, x3]
        add             x5, x0, x3, lsl #2
        str             d2, [x15]
        str             d2, [x15, x3]
        str             d2, [x5]
        add             x15, x5, x3, lsl #1
        str             d2, [x5, x3]
        str             d2, [x15]
        str             d2, [x15, x3]

        // Scatter-store column bytes with pre-computed addresses
        add             x5, x0, x3
        add             x6, x0, x3, lsl #1
        add             x7, x5, x3, lsl #1
        add             x8, x0, x3, lsl #2
        st1             {v5.b}[1], [x5]
        st1             {v5.b}[2], [x6]
        st1             {v5.b}[3], [x7]
        st1             {v5.b}[4], [x8]
        add             x5, x8, x3
        add             x6, x8, x3, lsl #1
        add             x7, x5, x3, lsl #1
        st1             {v5.b}[5], [x5]
        st1             {v5.b}[6], [x6]
        st1             {v5.b}[7], [x7]
        ret

2:      // === Chroma path: plain DC fill ===
        str             d2, [x0]
        add             x15, x0, x3, lsl #1
        str             d2, [x0, x3]
        add             x0, x0, x3, lsl #2
        str             d2, [x15]
        str             d2, [x15, x3]
        str             d2, [x0]
        add             x15, x0, x3, lsl #1
        str             d2, [x0, x3]
        str             d2, [x15]
        str             d2, [x15, x3]
        ret
endfunc

// -----------------------------------------------------------------------------
// pred_dc_16x16_8: DC prediction
// Arguments:
// x0: src
// x1: top
// x2: left
// x3: stride
// w4: c_idx
// -----------------------------------------------------------------------------
function ff_hevc_pred_dc_16x16_8_neon, export=1
        // Load top[0..15] and left[0..15]
        ldr             q0, [x1]                // top[0..15]
        ldr             q1, [x2]                // left[0..15]

        // Sum all pixels
        uaddlv          h2, v0.16b              // sum top
        uaddlv          h3, v1.16b              // sum left
        add             v2.4h, v2.4h, v3.4h

        // Add rounding and shift by 5
        urshr           v2.4h, v2.4h, #5        // (sum + 16) >> 5
        dup             v2.16b, v2.b[0]         // broadcast dc

        // Check if edge smoothing needed (luma only)
        cbnz            w4, 2f

        // === Luma path: fill + edge smoothing combined ===

        // Compute 3*dc in NEON domain (16-bit)
        uxtl            v3.8h, v2.8b            // widen dc to 16-bit
        add             v6.8h, v3.8h, v3.8h     // 2*dc
        add             v3.8h, v6.8h, v3.8h     // 3*dc

        // Widen top to 16-bit
        uxtl            v4.8h, v0.8b
        uxtl2           v5.8h, v0.16b

        // Corner: (top[0] + left[0] + 2*dc + 2) >> 2
        // Smoothed first row: (top[x] + 3*dc + 2) >> 2
        uxtl            v7.8h, v1.8b            // widen left[0..7] (reuse for corner lane 0)
        add             v16.4h, v4.4h, v7.4h    // corner: top[x] + left[x] (only lane 0 matters)
        add             v4.8h, v4.8h, v3.8h     // first row lo: top[x] + 3*dc
        add             v5.8h, v5.8h, v3.8h     // first row hi: top[x] + 3*dc
        add             v16.4h, v16.4h, v6.4h   // corner: + 2*dc
        rshrn           v4.8b, v4.8h, #2        // first row lo: >> 2
        rshrn2          v4.16b, v5.8h, #2       // first row hi: >> 2 (smoothed first row)
        rshrn           v16.8b, v16.8h, #2      // corner: >> 2
        // Overwrite corner byte
        ins             v4.b[0], v16.b[0]

        // Smoothed column: (left[y] + 3*dc + 2) >> 2
        uxtl            v5.8h, v1.8b
        uxtl2           v6.8h, v1.16b
        add             v5.8h, v5.8h, v3.8h
        add             v6.8h, v6.8h, v3.8h
        rshrn           v5.8b, v5.8h, #2
        rshrn2          v5.16b, v6.8h, #2       // smoothed column values

        // Store row 0 (smoothed)
        str             q4, [x0]

        // Store DC fill for all 15 remaining rows
        add             x15, x0, x3, lsl #1
        str             q2, [x0, x3]            // row 1
        add             x5, x0, x3, lsl #2
        str             q2, [x15]               // row 2
        str             q2, [x15, x3]           // row 3
        str             q2, [x5]                // row 4
        add             x15, x5, x3, lsl #1
        str             q2, [x5, x3]            // row 5
        add             x5, x5, x3, lsl #2
        str             q2, [x15]               // row 6
        str             q2, [x15, x3]           // row 7
        str             q2, [x5]                // row 8
        add             x15, x5, x3, lsl #1
        str             q2, [x5, x3]            // row 9
        add             x5, x5, x3, lsl #2
        str             q2, [x15]               // row 10
        str             q2, [x15, x3]           // row 11
        str             q2, [x5]                // row 12
        add             x15, x5, x3, lsl #1
        str             q2, [x5, x3]            // row 13
        str             q2, [x15]               // row 14
        str             q2, [x15, x3]           // row 15

        // Now scatter-store column bytes over the DC fill
        add             x5, x0, x3
        add             x6, x0, x3, lsl #1
        add             x7, x5, x3, lsl #1
        add             x8, x0, x3, lsl #2
        st1             {v5.b}[1], [x5]
        st1             {v5.b}[2], [x6]
        st1             {v5.b}[3], [x7]
        st1             {v5.b}[4], [x8]
        add             x5, x8, x3
        add             x6, x8, x3, lsl #1
        add             x7, x5, x3, lsl #1
        add             x9, x8, x3, lsl #2
        st1             {v5.b}[5], [x5]
        st1             {v5.b}[6], [x6]
        st1             {v5.b}[7], [x7]
        st1             {v5.b}[8], [x9]
        add             x5, x9, x3
        add             x6, x9, x3, lsl #1
        add             x7, x5, x3, lsl #1
        add             x8, x9, x3, lsl #2
        st1             {v5.b}[9], [x5]
        st1             {v5.b}[10], [x6]
        st1             {v5.b}[11], [x7]
        st1             {v5.b}[12], [x8]
        add             x5, x8, x3
        add             x6, x8, x3, lsl #1
        add             x7, x5, x3, lsl #1
        st1             {v5.b}[13], [x5]
        st1             {v5.b}[14], [x6]
        st1             {v5.b}[15], [x7]
        ret

2:      // === Chroma path: plain DC fill ===
        str             q2, [x0]                // row 0
        add             x15, x0, x3, lsl #1
        str             q2, [x0, x3]            // row 1
        add             x5, x0, x3, lsl #2
        str             q2, [x15]               // row 2
        str             q2, [x15, x3]           // row 3
        str             q2, [x5]                // row 4
        add             x15, x5, x3, lsl #1
        str             q2, [x5, x3]            // row 5
        add             x5, x5, x3, lsl #2
        str             q2, [x15]               // row 6
        str             q2, [x15, x3]           // row 7
        str             q2, [x5]                // row 8
        add             x15, x5, x3, lsl #1
        str             q2, [x5, x3]            // row 9
        add             x5, x5, x3, lsl #2
        str             q2, [x15]               // row 10
        str             q2, [x15, x3]           // row 11
        str             q2, [x5]                // row 12
        add             x15, x5, x3, lsl #1
        str             q2, [x5, x3]            // row 13
        str             q2, [x15]               // row 14
        str             q2, [x15, x3]           // row 15
        ret
endfunc

// -----------------------------------------------------------------------------
// pred_dc_32x32_8: DC prediction (no edge smoothing)
// Arguments:
// x0: src
// x1: top
// x2: left
// x3: stride
// w4: c_idx
// -----------------------------------------------------------------------------
function ff_hevc_pred_dc_32x32_8_neon, export=1
        // Load top[0..31] and left[0..31]
        ldp             q0, q1, [x1]            // top[0..31]
        ldp             q2, q3, [x2]            // left[0..31]

        // Sum all pixels
        uaddlv          h0, v0.16b
        uaddlv          h1, v1.16b
        uaddlv          h2, v2.16b
        uaddlv          h3, v3.16b
        add             v0.4h, v0.4h, v1.4h
        add             v2.4h, v2.4h, v3.4h
        add             v0.4h, v0.4h, v2.4h

        // Add rounding and shift by 6 (urshr = unsigned rounding shift right)
        urshr           v0.4h, v0.4h, #6
        dup             v0.16b, v0.b[0]
        mov             v1.16b, v0.16b

        // Store 32 rows
        mov             w6, #32
2:
        subs            w6, w6, #1
        stp             q0, q1, [x0]
        add             x0, x0, x3
        b.ne            2b

        // No edge smoothing for 32x32 (size >= 32)
        ret
endfunc

// =============================================================================
// Planar Prediction
// =============================================================================

/*
 * Planar prediction algorithm:
 * For each pixel (x, y):
 * POS(x,y) = ((size-1-x)*left[y] + (x+1)*top[size] +
 *             (size-1-y)*top[x] + (y+1)*left[size] + size) >> (log2_size+1)
 */
// -----------------------------------------------------------------------------
// pred_planar_4x4_8: Planar prediction
// Arguments:
// x0: src
// x1: top
// x2: left
// x3: stride
// -----------------------------------------------------------------------------
function ff_hevc_pred_planar_4x4_8_neon, export=1
        // Load reference samples
        ldr             s0, [x1]                // top[0..3]
        ldr             s1, [x2]                // left[0..3]
        ldrb            w4, [x1, #4]            // top[4]
        ldrb            w5, [x2, #4]            // left[4]

        // Setup weight vectors for x direction
        movrel          x6, planar_weights_4
        ldp             d4, d5, [x6]            // weights_dec, weights_inc

        // Precompute base[x] = inc[x]*top[4] + 3*top[x] + left[4] + 4
        dup             v6.8b, w4               // top[4]
        umull           v2.8h, v5.8b, v6.8b     // inc[x]*top[4]
        uxtl            v3.8h, v0.8b            // widen top[x]
        ushll           v6.8h, v0.8b, #1        // top[x]<<1
        add             v6.8h, v6.8h, v3.8h     // 3*top[x]
        add             v2.8h, v2.8h, v6.8h     // + inc*top[4]
        dup             v5.8h, w5               // left[4] as 16-bit
        add             v2.8h, v2.8h, v5.8h     // + left[4]  (y=0: (y+1)*left[4]=1*left[4])
        // v2 = base for row 0 (rounding folded into rshrn below)

        // Precompute decrement: top[x] - left[4]
        sub             v5.8h, v3.8h, v5.8h     // decrement = top[x] - left[4]

        // Pre-dup all left values
        dup             v16.8b, v1.b[0]         // left[0]
        dup             v17.8b, v1.b[1]         // left[1]
        dup             v18.8b, v1.b[2]         // left[2]
        dup             v19.8b, v1.b[3]         // left[3]

        // Compute bases for all 4 rows
        mov             v3.16b, v2.16b          // base0 = base
        sub             v20.8h, v2.8h, v5.8h    // base1 = base - dec
        sub             v21.8h, v20.8h, v5.8h   // base2 = base1 - dec
        sub             v22.8h, v21.8h, v5.8h   // base3 = base2 - dec

        // Interleaved row 0/1 computation
        umull           v6.8h, v4.8b, v16.8b    // row0: dec*left[0]
        umull           v7.8h, v4.8b, v17.8b    // row1: dec*left[1]
        add             v6.8h, v6.8h, v3.8h     // row0: + base0
        add             v7.8h, v7.8h, v20.8h    // row1: + base1
        rshrn           v6.8b, v6.8h, #3        // row0: >>3
        rshrn           v7.8b, v7.8h, #3        // row1: >>3

        // Interleaved row 2/3 computation
        umull           v23.8h, v4.8b, v18.8b   // row2: dec*left[2]
        st1             {v6.s}[0], [x0], x3     // store row0
        umull           v24.8h, v4.8b, v19.8b   // row3: dec*left[3]
        st1             {v7.s}[0], [x0], x3     // store row1
        add             v23.8h, v23.8h, v21.8h  // row2: + base2
        add             v24.8h, v24.8h, v22.8h  // row3: + base3
        rshrn           v23.8b, v23.8h, #3      // row2
        rshrn           v24.8b, v24.8h, #3      // row3
        st1             {v23.s}[0], [x0], x3    // store row2
        str             s24, [x0]               // store row3

        ret
endfunc

// -----------------------------------------------------------------------------
// pred_planar_8x8_8: Planar prediction
// Arguments:
// x0: src
// x1: top
// x2: left
// x3: stride
// -----------------------------------------------------------------------------
function ff_hevc_pred_planar_8x8_8_neon, export=1
        // Load reference samples
        ldr             d0, [x1]                // top[0..7]
        ldr             d1, [x2]                // left[0..7]
        ldrb            w4, [x1, #8]            // top[8]
        ldrb            w5, [x2, #8]            // left[8]

        // Setup weight vectors
        movrel          x6, planar_weights_8
        ldp             d4, d5, [x6]            // weights_dec, weights_inc

        // Precompute base[x] = inc[x]*top[8] + 7*top[x] + left[8] + 8
        dup             v6.8b, w4               // top[8]
        umull           v2.8h, v5.8b, v6.8b     // inc[x]*top[8]
        uxtl            v3.8h, v0.8b            // widen top[x]
        ushll           v6.8h, v0.8b, #3        // top[x]<<3
        sub             v6.8h, v6.8h, v3.8h     // 7*top[x]
        add             v2.8h, v2.8h, v6.8h     // + inc*top[8]
        dup             v5.8h, w5               // left[8] as 16-bit
        add             v2.8h, v2.8h, v5.8h     // + left[8]
        // v2 = base for row 0 (rounding folded into rshrn below)

        // Precompute decrement: top[x] - left[8]
        sub             v5.8h, v3.8h, v5.8h     // decrement

        // Precompute all 8 row bases to break serial dependency chain
        mov             v16.16b, v2.16b         // base0
        sub             v17.8h, v16.8h, v5.8h   // base1
        sub             v18.8h, v17.8h, v5.8h   // base2
        sub             v19.8h, v18.8h, v5.8h   // base3
        sub             v20.8h, v19.8h, v5.8h   // base4
        sub             v21.8h, v20.8h, v5.8h   // base5
        sub             v22.8h, v21.8h, v5.8h   // base6
        sub             v23.8h, v22.8h, v5.8h   // base7

        // Rows 0-1
        dup             v6.8b, v1.b[0]
        dup             v7.8b, v1.b[1]
        umull           v2.8h, v4.8b, v6.8b
        umull           v3.8h, v4.8b, v7.8b
        add             v2.8h, v2.8h, v16.8h
        add             v3.8h, v3.8h, v17.8h
        rshrn           v2.8b, v2.8h, #4
        rshrn           v3.8b, v3.8h, #4
        st1             {v2.d}[0], [x0], x3
        st1             {v3.d}[0], [x0], x3

        // Rows 2-3
        dup             v6.8b, v1.b[2]
        dup             v7.8b, v1.b[3]
        umull           v2.8h, v4.8b, v6.8b
        umull           v3.8h, v4.8b, v7.8b
        add             v2.8h, v2.8h, v18.8h
        add             v3.8h, v3.8h, v19.8h
        rshrn           v2.8b, v2.8h, #4
        rshrn           v3.8b, v3.8h, #4
        st1             {v2.d}[0], [x0], x3
        st1             {v3.d}[0], [x0], x3

        // Rows 4-5
        dup             v6.8b, v1.b[4]
        dup             v7.8b, v1.b[5]
        umull           v2.8h, v4.8b, v6.8b
        umull           v3.8h, v4.8b, v7.8b
        add             v2.8h, v2.8h, v20.8h
        add             v3.8h, v3.8h, v21.8h
        rshrn           v2.8b, v2.8h, #4
        rshrn           v3.8b, v3.8h, #4
        st1             {v2.d}[0], [x0], x3
        st1             {v3.d}[0], [x0], x3

        // Rows 6-7
        dup             v6.8b, v1.b[6]
        dup             v7.8b, v1.b[7]
        umull           v2.8h, v4.8b, v6.8b
        umull           v3.8h, v4.8b, v7.8b
        add             v2.8h, v2.8h, v22.8h
        add             v3.8h, v3.8h, v23.8h
        rshrn           v2.8b, v2.8h, #4
        rshrn           v3.8b, v3.8h, #4
        st1             {v2.d}[0], [x0], x3
        str             d3, [x0]

        ret
endfunc

// -----------------------------------------------------------------------------
// pred_planar_16x16_8: Planar prediction
//
// Formula: POS(x,y) = ((15-x)*left[y] + (x+1)*top[16]
//                     + (15-y)*top[x]  + (y+1)*left[16] + 16) >> 5
//
// Decomposed into incremental base update (same as 4x4/8x8/32x32):
//   base_0[x] = inc[x]*top[16] + 15*top[x] + left[16] + 16
//   base_{y+1}[x] = base_y[x] - (top[x] - left[16])
//   POS(x,y) = (base_y[x] + weights_dec[x]*left[y]) >> 5
//
// 16-wide requires split-half processing (umull/umull2).
// 16 rows fully unrolled with NEON-domain left[y] broadcast.
//
// Arguments:
// x0: src
// x1: top
// x2: left
// x3: stride
// -----------------------------------------------------------------------------
function ff_hevc_pred_planar_16x16_8_neon, export=1
        // Load reference samples
        ldr             q0, [x1]                // top[0..15]
        ldr             q1, [x2]                // left[0..15]
        ldrb            w4, [x1, #16]           // top[16]
        ldrb            w5, [x2, #16]           // left[16]

        // Setup weight vectors for 16 elements
        movrel          x6, planar_weights_16
        ldp             q4, q5, [x6]            // weights_dec [15..0], weights_inc [1..16]

        // Precompute base[x] = inc[x]*top[16] + 15*top[x] + left[16]
        dup             v6.16b, w4              // top[16] broadcast
        umull           v19.8h, v5.8b, v6.8b    // inc[x]*top[16] lo
        umull2          v20.8h, v5.16b, v6.16b  // inc[x]*top[16] hi

        // 15*top[x] = (top[x]<<4) - top[x]
        uxtl            v2.8h, v0.8b            // widen top[0..7]
        uxtl2           v3.8h, v0.16b           // widen top[8..15]
        ushll           v6.8h, v0.8b, #4        // top[0..7]<<4
        ushll2          v7.8h, v0.16b, #4       // top[8..15]<<4
        sub             v6.8h, v6.8h, v2.8h     // 15*top[0..7]
        sub             v7.8h, v7.8h, v3.8h     // 15*top[8..15]

        add             v19.8h, v19.8h, v6.8h   // + 15*top lo
        add             v20.8h, v20.8h, v7.8h   // + 15*top hi

        dup             v5.8h, w5               // left[16] as 16-bit
        add             v19.8h, v19.8h, v5.8h   // + left[16]
        add             v20.8h, v20.8h, v5.8h
        // v19/v20 = base_lo/base_hi for row 0 (rounding folded into rshrn below)

        // Precompute decrement: top[x] - left[16]
        sub             v21.8h, v2.8h, v5.8h    // dec lo = top[0..7] - left[16]
        sub             v22.8h, v3.8h, v5.8h    // dec hi = top[8..15] - left[16]

        // Persistent registers:
        //   v19,v20 = base[0..15] (16-bit, decremented each row)
        //   v21,v22 = decrement[0..15] (16-bit, constant)
        //   v4      = weight_dec[0..15] (8-bit, constant)
        //   v1      = left[0..15] (8-bit, preloaded for NEON-domain broadcast)

.macro planar16_row lane, last=0
        dup             v6.16b, v1.b[\lane]     // left[y] NEON-domain broadcast
        umull           v16.8h, v4.8b, v6.8b    // dec[x]*left[y] lo
        umull2          v17.8h, v4.16b, v6.16b  // dec[x]*left[y] hi
        add             v16.8h, v16.8h, v19.8h  // + base lo
        add             v17.8h, v17.8h, v20.8h  // + base hi
        rshrn           v16.8b, v16.8h, #5      // >>5 lo (rounded)
        rshrn2          v16.16b, v17.8h, #5     // >>5 hi, merge into q16
  .if \last == 0
        st1             {v16.16b}, [x0], x3
        sub             v19.8h, v19.8h, v21.8h  // base -= decrement
        sub             v20.8h, v20.8h, v22.8h
  .else
        str             q16, [x0]
  .endif
.endm

        planar16_row    0
        planar16_row    1
        planar16_row    2
        planar16_row    3
        planar16_row    4
        planar16_row    5
        planar16_row    6
        planar16_row    7
        planar16_row    8
        planar16_row    9
        planar16_row    10
        planar16_row    11
        planar16_row    12
        planar16_row    13
        planar16_row    14
        planar16_row    15, last=1

.purgem planar16_row

        ret
endfunc

// -----------------------------------------------------------------------------
// pred_planar_32x32_8: Planar prediction
//
// Formula: POS(x,y) = ((31-x)*left[y] + (x+1)*top[32]
//                     + (31-y)*top[x]  + (y+1)*left[32] + 32) >> 6
//
// Decomposed as:  base[x] = weight_inc[x]*top[32] + 31*top[x] + 32
//                 Per row:  base[x] += left[32]    (incremental for (y+1)*left[32])
//                           base[x] -= top[x]      (incremental for (31-y)*top[x])
//                           result   = base[x] + weight_dec[x]*left[y]
//
// Both row_add and the (31-y)*top[x] term are folded into the base,
// eliminating all GP→NEON scalar broadcasts except for left[y].
// The loop processes 8 rows per iteration (macro-expanded), with 4
// iterations total. left[y] values are loaded 8 at a time and
// broadcast per-lane within each iteration.
//
// Arguments:
// x0: src
// x1: top
// x2: left
// x3: stride
// -----------------------------------------------------------------------------
function ff_hevc_pred_planar_32x32_8_neon, export=1
        // Load top[0..31]
        ldp             q0, q1, [x1]            // top[0..15], top[16..31]
        ldrb            w4, [x1, #32]           // top[32]
        ldrb            w5, [x2, #32]           // left[32]

        // Load weight vectors
        movrel          x6, planar_weights_32
        ldp             q4, q5, [x6]            // weight_dec = {31,30,...,0}
        ldp             q6, q7, [x6, #32]       // weight_inc = {1,2,...,32}

        // Precompute term_A = weight_inc * top[32]  (16-bit)
        dup             v2.16b, w4
        umull           v20.8h, v6.8b, v2.8b
        umull2          v21.8h, v6.16b, v2.16b
        umull           v22.8h, v7.8b, v2.8b
        umull2          v23.8h, v7.16b, v2.16b

        // Widen top[x] for incremental subtraction
        uxtl            v24.8h, v0.8b
        uxtl2           v25.8h, v0.16b

        // 31*top[x] = top[x]<<5 - top[x]
        ushll           v6.8h, v0.8b, #5
        ushll2          v7.8h, v0.16b, #5
        sub             v6.8h, v6.8h, v24.8h    // 31*top[0..7]
        sub             v7.8h, v7.8h, v25.8h    // 31*top[8..15]

        // base[0..15] = term_A + 31*top[0..15]
        add             v20.8h, v20.8h, v6.8h
        add             v21.8h, v21.8h, v7.8h

        // Same for top[16..31]
        uxtl            v26.8h, v1.8b
        uxtl2           v27.8h, v1.16b
        ushll           v6.8h, v1.8b, #5
        ushll2          v7.8h, v1.16b, #5
        sub             v6.8h, v6.8h, v26.8h     // 31*top[16..23]
        sub             v7.8h, v7.8h, v27.8h     // 31*top[24..31]
        add             v22.8h, v22.8h, v6.8h
        add             v23.8h, v23.8h, v7.8h

        // Compute combined decrement: top[x] - left[32]
        // Each row: base += left[32] and base -= top[x]
        // Combined: base -= (top[x] - left[32])
        dup             v3.8h, w5               // left[32] as 16-bit
        sub             v24.8h, v24.8h, v3.8h   // top[0..7] - left[32]
        sub             v25.8h, v25.8h, v3.8h   // top[8..15] - left[32]
        sub             v26.8h, v26.8h, v3.8h   // top[16..23] - left[32]
        sub             v27.8h, v27.8h, v3.8h   // top[24..31] - left[32]

        // Now base needs initial +=left[32] for y=0 (row_add = 1*left[32])
        add             v20.8h, v20.8h, v3.8h
        add             v21.8h, v21.8h, v3.8h
        add             v22.8h, v22.8h, v3.8h
        add             v23.8h, v23.8h, v3.8h

        // Persistent registers:
        //   v20-v23 = base[0..31] (includes running row_add, decremented by combined each row)
        //   v24,v25 = top[0..15] - left[32] (combined decrement)
        //   v26,v27 = top[16..31] - left[32] (combined decrement)
        //   v4,v5   = weight_dec[0..31] (8-bit)

.macro planar32_row lane, leftreg
        dup             v2.16b, \leftreg\().b[\lane]
        umull           v16.8h, v4.8b, v2.8b
        umull2          v17.8h, v4.16b, v2.16b
        umull           v18.8h, v5.8b, v2.8b
        umull2          v19.8h, v5.16b, v2.16b
        add             v16.8h, v16.8h, v20.8h
        add             v17.8h, v17.8h, v21.8h
        add             v18.8h, v18.8h, v22.8h
        add             v19.8h, v19.8h, v23.8h
        rshrn           v28.8b, v16.8h, #6
        rshrn2          v28.16b, v17.8h, #6
        rshrn           v29.8b, v18.8h, #6
        rshrn2          v29.16b, v19.8h, #6
        stp             q28, q29, [x0]
        add             x0, x0, x3
        sub             v20.8h, v20.8h, v24.8h
        sub             v21.8h, v21.8h, v25.8h
        sub             v22.8h, v22.8h, v26.8h
        sub             v23.8h, v23.8h, v27.8h
.endm

        // Process 32 rows in 4 iterations of 8 rows each
        mov             w7, #4
.Lplanar32_loop:
        ld1             {v1.8b}, [x2], #8       // load 8 left[] bytes for this iteration

        planar32_row    0, v1
        planar32_row    1, v1
        planar32_row    2, v1
        planar32_row    3, v1
        planar32_row    4, v1
        planar32_row    5, v1
        planar32_row    6, v1
        planar32_row    7, v1

        subs            w7, w7, #1
        b.gt            .Lplanar32_loop

.purgem planar32_row

        ret
endfunc


// =============================================================================
// Weight tables for planar prediction
// =============================================================================

const planar_weights_4, align=4
        .byte   3, 2, 1, 0, 0, 0, 0, 0          // weights_dec for 4x4
        .byte   1, 2, 3, 4, 0, 0, 0, 0          // weights_inc for 4x4
endconst

const planar_weights_8, align=4
        .byte   7, 6, 5, 4, 3, 2, 1, 0          // weights_dec
        .byte   1, 2, 3, 4, 5, 6, 7, 8          // weights_inc
endconst

const planar_weights_16, align=4
        .byte   15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
        .byte   1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
endconst

const planar_weights_32, align=4
        .byte   31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16
        .byte   15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
        .byte   1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
        .byte   17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32
endconst

// =============================================================================
// Reference Sample Filtering — 3-tap filter [1,2,1]>>2
// =============================================================================

/*
 * 3-tap filter: out[i] = (in[i-1] + 2*in[i] + in[i+1] + 2) >> 2
 *
 * Signature: void ff_hevc_ref_filter_3tap_NxN_8_neon(
 *     uint8_t *filtered_left,   // x0
 *     uint8_t *filtered_top,    // x1
 *     const uint8_t *left,      // x2
 *     const uint8_t *top,       // x3
 *     int size)                 // w4 — (unused, size is hardcoded per entry point)
 *
 * All pointers point to index [0]; caller has adjusted for [-1] accessibility.
 *
 * NEON approach: load 32 bytes via ld1, use ext #1/#2 to form
 *   prev(v0)/curr(v2)/next(v3). Fold the 3-tap kernel into two
 *   halving-adds on 16 bytes (no widen/narrow):
 *     uhadd  v4, v0, v3        // floor((prev + next) / 2)
 *     urhadd v4, v4, v2        // (h + curr + 1) / 2
 *   equivalent to (prev + 2*curr + next + 2) >> 2 for all u8 inputs.
 *   Caller pads input arrays by 16 bytes to guarantee safe 32-byte loads.
 *   Forward processing is safe since output goes to a separate buffer.
 */

// Load 32 bytes from [x12], compute 16 filtered samples into v4.16b
.macro filter_3tap_compute
        ld1             {v0.16b, v1.16b}, [x12]
        ext             v2.16b, v0.16b, v1.16b, #1 // curr
        ext             v3.16b, v0.16b, v1.16b, #2 // next
        uhadd           v4.16b, v0.16b, v3.16b   // floor((prev + next) / 2)
        urhadd          v4.16b, v4.16b, v2.16b   // (prev + 2*curr + next + 2) >> 2
.endm

// Filter one array. n_reg holds 2*size; filters n-1 samples, copies last.
// Caller guarantees 16 bytes of readable padding beyond in[n-1].
// For total >= 16, overlap-last-16 avoids partial stores.
// For total < 16 (8x8), overlap-last-8 stores two 8-byte halves.
.macro filter_3tap_array x_out, x_in, n_reg
        // Copy last element: out[n-1] = in[n-1]
        sub             x10, \n_reg, #1
        ldrb            w11, [\x_in, x10]
        strb            w11, [\x_out, x10]

        sub             x12, \x_in, #1          // x12 = load ptr (&in[-1])
        mov             x13, \x_out             // x13 = store ptr (&out[0])
        sub             x14, \n_reg, #1         // x14 = total samples to filter

        cmp             x14, #16
        b.lt            6f                      // total < 16: small path (8x8)

        // --- Main loop: 16 samples per iteration ---
1:      filter_3tap_compute
        st1             {v4.16b}, [x13], #16
        add             x12, x12, #16
        sub             x14, x14, #16
        cmp             x14, #16
        b.ge            1b

        // --- Overlap last 16 samples ---
        cbz             x14, 9f
        add             x12, \x_in, \n_reg
        sub             x12, x12, #18          // &in[n-18]
        add             x13, \x_out, \n_reg
        sub             x13, x13, #17          // &out[n-17]
        filter_3tap_compute
        st1             {v4.16b}, [x13]
        b               9f

        // --- Small path: total < 16 (8x8, total=15) ---
        // Store as two overlapping 8-byte halves: out[0..7] + out[7..14]
6:      filter_3tap_compute
        ext             v5.16b, v4.16b, v4.16b, #7
        str             d4, [x13]               // out[0..7]
        str             d5, [x13, #7]           // out[7..14]
9:
.endm

// Corner: filtered[-1] = (left[0] + 2*left[-1] + top[0] + 2) >> 2
.macro filter_3tap_corner
        ldrb            w10, [x2]               // left[0]
        ldrb            w11, [x2, #-1]          // left[-1]
        ldrb            w12, [x3]               // top[0]
        add             w10, w10, w12           // left[0] + top[0]
        add             w10, w10, w11, lsl #1   // + 2*left[-1]
        add             w10, w10, #2            // + 2
        lsr             w10, w10, #2            // >> 2
        strb            w10, [x0, #-1]          // filtered_left[-1]
        strb            w10, [x1, #-1]          // filtered_top[-1]
.endm

function ff_hevc_ref_filter_3tap_8x8_8_neon, export=1
        mov             x4, #16                 // n = 2*8
        b               hevc_ref_filter_3tap_common_8_neon
endfunc

function ff_hevc_ref_filter_3tap_16x16_8_neon, export=1
        mov             x4, #32                 // n = 2*16
        b               hevc_ref_filter_3tap_common_8_neon
endfunc

function ff_hevc_ref_filter_3tap_32x32_8_neon, export=1
        mov             x4, #64                 // n = 2*32
        b               hevc_ref_filter_3tap_common_8_neon
endfunc

function hevc_ref_filter_3tap_common_8_neon, export=0
        filter_3tap_array x0, x2, x4
        filter_3tap_corner
        filter_3tap_array x1, x3, x4
        ret
endfunc

.purgem filter_3tap_compute
.purgem filter_3tap_array
.purgem filter_3tap_corner

// =============================================================================
// Reference Sample Filtering — Strong Intra Smoothing
// =============================================================================

/*
 * Strong intra smoothing (32x32 luma only):
 *   filtered_top[i] = ((64-(i+1))*top[-1] + (i+1)*top[63] + 32) >> 6
 *   left[i]         = ((64-(i+1))*left[-1] + (i+1)*left[63] + 32) >> 6
 *   for i = 0..62
 *
 * Also sets: filtered_top[-1] = top[-1], filtered_top[63] = top[63]
 *
 * Signature: void ff_hevc_ref_filter_strong_8_neon(
 *     uint8_t *filtered_top,  // x0
 *     uint8_t *left,          // x1
 *     const uint8_t *top)     // x2
 *
 * NEON approach:
 *   Preloaded weights dec={63..1,0} in v0-v3, inc={1..63,0} in v4-v7.
 *   Two 16-byte blocks interleaved per pair, written via st1 {v22,v23},
 *   hiding the rshrn→st1 latency. Weight[63]=0 lets lane 15 fall out as
 *   zero; one strb restores output[63] to the correct end value.
 */

// Precomputed weight tables in .rodata
const filter_strong_weights_inc, align=4
        .byte    1,  2,  3,  4,  5,  6,  7,  8
        .byte    9, 10, 11, 12, 13, 14, 15, 16
        .byte   17, 18, 19, 20, 21, 22, 23, 24
        .byte   25, 26, 27, 28, 29, 30, 31, 32
        .byte   33, 34, 35, 36, 37, 38, 39, 40
        .byte   41, 42, 43, 44, 45, 46, 47, 48
        .byte   49, 50, 51, 52, 53, 54, 55, 56
        .byte   57, 58, 59, 60, 61, 62, 63,  0
endconst

const filter_strong_weights_dec, align=4
        .byte   63, 62, 61, 60, 59, 58, 57, 56
        .byte   55, 54, 53, 52, 51, 50, 49, 48
        .byte   47, 46, 45, 44, 43, 42, 41, 40
        .byte   39, 38, 37, 36, 35, 34, 33, 32
        .byte   31, 30, 29, 28, 27, 26, 25, 24
        .byte   23, 22, 21, 20, 19, 18, 17, 16
        .byte   15, 14, 13, 12, 11, 10,  9,  8
        .byte    7,  6,  5,  4,  3,  2,  1,  0
endconst

// Macro: apply strong smoothing to one 63-element array
// x_out = output pointer (index 0), w_start = start pixel, w_end = end pixel
// Weights must be preloaded: v0-v3 = dec weights, v4-v7 = inc weights (16b each)
// Interleave two 16-byte blocks to hide rshrn→st1 latency on in-order cores.
.macro strong_smooth x_out, w_start, w_end
        dup             v16.16b, \w_start       // broadcast start
        dup             v17.16b, \w_end         // broadcast end
        mov             x14, \x_out

        // Blocks 0+1 interleaved: indices 0-31
        umull           v18.8h, v0.8b, v16.8b   // blk0: dec_lo * start
        umull           v20.8h, v1.8b, v16.8b   // blk1: dec_lo * start
        umull2          v19.8h, v0.16b, v16.16b // blk0: dec_hi * start
        umull2          v21.8h, v1.16b, v16.16b // blk1: dec_hi * start
        umlal           v18.8h, v4.8b, v17.8b   // blk0: + inc_lo * end
        umlal           v20.8h, v5.8b, v17.8b   // blk1: + inc_lo * end
        umlal2          v19.8h, v4.16b, v17.16b // blk0: + inc_hi * end
        umlal2          v21.8h, v5.16b, v17.16b // blk1: + inc_hi * end
        rshrn           v22.8b, v18.8h, #6      // blk0: narrow lo
        rshrn           v23.8b, v20.8h, #6      // blk1: narrow lo
        rshrn2          v22.16b, v19.8h, #6     // blk0: narrow hi
        rshrn2          v23.16b, v21.8h, #6     // blk1: narrow hi
        st1             {v22.16b, v23.16b}, [x14], #32

        // Blocks 2+3 interleaved: indices 32-63
        // Weight at index 63 is zero, so v23.b[15] computes 0; we store all
        // 32 bytes and fix up output[63] with a scalar strb afterward.
        umull           v18.8h, v2.8b, v16.8b   // blk2: dec_lo * start
        umull           v20.8h, v3.8b, v16.8b   // blk3: dec_lo * start
        umull2          v19.8h, v2.16b, v16.16b // blk2: dec_hi * start
        umull2          v21.8h, v3.16b, v16.16b // blk3: dec_hi * start
        umlal           v18.8h, v6.8b, v17.8b   // blk2: + inc_lo * end
        umlal           v20.8h, v7.8b, v17.8b   // blk3: + inc_lo * end
        umlal2          v19.8h, v6.16b, v17.16b // blk2: + inc_hi * end
        umlal2          v21.8h, v7.16b, v17.16b // blk3: + inc_hi * end
        rshrn           v22.8b, v18.8h, #6      // blk2: narrow lo
        rshrn           v23.8b, v20.8h, #6      // blk3: narrow lo
        rshrn2          v22.16b, v19.8h, #6     // blk2: narrow hi
        rshrn2          v23.16b, v21.8h, #6     // blk3: narrow hi
        st1             {v22.16b, v23.16b}, [x14] // indices 32-63
        strb            \w_end, [x14, #31]      // restore output[63]
.endm

function ff_hevc_ref_filter_strong_8_neon, export=1
        ldrb            w10, [x2, #-1]          // top[-1]
        strb            w10, [x0, #-1]          // filtered_top[-1]
        ldrb            w11, [x2, #63]          // top[63]
        strb            w11, [x0, #63]          // filtered_top[63]

        // Preload all weight tables into registers (shared across both passes)
        movrel          x8, filter_strong_weights_dec
        movrel          x9, filter_strong_weights_inc
        ld1             {v0.16b, v1.16b, v2.16b, v3.16b}, [x8]
        ld1             {v4.16b, v5.16b, v6.16b, v7.16b}, [x9]

        // Smooth top → filtered_top[0..62]
        strong_smooth   x0, w10, w11

        // Smooth left → left[0..62] in-place
        ldrb            w10, [x1, #-1]          // left[-1]
        ldrb            w11, [x1, #63]          // left[63]
        strong_smooth   x1, w10, w11

        ret
endfunc

.purgem strong_smooth

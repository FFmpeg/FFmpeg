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

/* -*-arm64-*-
 * vim: syntax=arm64asm
 *
 * AArch64 NEON optimised SAO functions for HEVC decoding
 *
 * Copyright (c) 2020 Josh Dekker <josh@itanimul.li>
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

// void sao_band_filter(uint8_t *_dst, uint8_t *_src,
//                      ptrdiff_t stride_dst, ptrdiff_t stride_src,
//                      int16_t *sao_offset_val, int sao_left_class,
//                      int width, int height)
function ff_hevc_sao_band_filter_8x8_8_neon, export=1
        sub             sp,  sp, #64
        stp             xzr, xzr, [sp]
        stp             xzr, xzr, [sp, #16]
        stp             xzr, xzr, [sp, #32]
        stp             xzr, xzr, [sp, #48]
        mov             w8,  #4
0:      ldrsh           x9, [x4,  x8, lsl #1]      // sao_offset_val[k+1]
        subs            w8,  w8,  #1
        add             w10, w8,  w5               // k + sao_left_class
        and             w10, w10, #0x1F
        strh            w9, [sp, x10, lsl #1]
        bne             0b
        ld1             {v16.16b-v19.16b}, [sp], #64
        movi            v20.8h,   #1
1:      mov             w8,  w6                    // beginning of line
2:      // Simple layout for accessing 16bit values
        // with 8bit LUT.
        //
        //   00  01  02  03  04  05  06  07
        // +----------------------------------->
        // |xDE#xAD|xCA#xFE|xBE#xEF|xFE#xED|....
        // +----------------------------------->
        //    i-0     i-1     i-2     i-3
        ld1             {v2.8b}, [x1]              // dst[x] = av_clip_pixel(src[x] + offset_table[src[x] >> shift]);
        uxtl            v0.8h,  v2.8b              // load src[x]
        ushr            v2.8h,  v0.8h, #3          // >> BIT_DEPTH - 3
        shl             v1.8h,  v2.8h, #1          // low (x2, accessing short)
        add             v3.8h,  v1.8h, v20.8h      // +1 access upper short
        sli             v1.8h,  v3.8h, #8          // shift insert index to upper byte
        tbx             v2.16b, {v16.16b-v19.16b}, v1.16b // table
        add             v1.8h,  v0.8h, v2.8h       // src[x] + table
        sqxtun          v4.8b,  v1.8h              // clip + narrow
        st1             {v4.8b}, [x0]              // store
        subs            w8, w8,  #8                // done 8 pixels
        bne             2b
        subs            w7, w7,  #1                // finished line, prep. new
        add             x0, x0,  x2                // dst += stride_dst
        add             x1, x1,  x3                // src += stride_src
        bne             1b
        ret
endfunc

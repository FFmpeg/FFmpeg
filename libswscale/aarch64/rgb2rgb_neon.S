/*
 * Copyright (c) 2020 Martin Storsjo
 * Copyright (c) 2024 Ramiro Polla
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

#define RGB2YUV_COEFFS 16*4+16*32
#define BY v0.h[0]
#define GY v0.h[1]
#define RY v0.h[2]
#define BU v1.h[0]
#define GU v1.h[1]
#define RU v1.h[2]
#define BV v2.h[0]
#define GV v2.h[1]
#define RV v2.h[2]
#define Y_OFFSET  v22
#define UV_OFFSET v23

const shuf_0321_tbl, align=4
        .byte  0,  3,  2,  1
        .byte  4,  7,  6,  5
        .byte  8,  11, 10, 9
        .byte  12, 15, 14, 13
endconst

const shuf_1230_tbl, align=4
        .byte  1,  2,  3,  0
        .byte  5,  6,  7,  4
        .byte  9,  10, 11, 8
        .byte  13, 14, 15, 12
endconst

const shuf_2103_tbl, align=4
        .byte  2,  1,  0,  3
        .byte  6,  5,  4,  7
        .byte  10, 9,  8,  11
        .byte  14, 13, 12, 15
endconst

const shuf_3012_tbl, align=4
        .byte  3,  0,  1,  2
        .byte  7,  4,  5,  6
        .byte  11, 8,  9,  10
        .byte  15, 12, 13, 14
endconst

const shuf_3210_tbl, align=4
        .byte  3,  2,  1,  0
        .byte  7,  6,  5,  4
        .byte  11, 10, 9,  8
        .byte  15, 14, 13, 12
endconst

const shuf_3102_tbl, align=4
        .byte  3,  1,  0,  2
        .byte  7,  5,  4,  6
        .byte  11, 9,  8,  10
        .byte  15, 13, 12, 14
endconst

const shuf_2013_tbl, align=4
        .byte  2,  0,  1,  3
        .byte  6,  4,  5,  7
        .byte  10, 8,  9,  11
        .byte  14, 12, 13, 15
endconst

const shuf_1203_tbl, align=4
        .byte  1,  2,  0,  3
        .byte  5,  6,  4,  7
        .byte  9,  10, 8,  11
        .byte  13, 14, 12, 15
endconst

const shuf_2130_tbl, align=4
        .byte  2,  1,  3,  0
        .byte  6,  5,  7,  4
        .byte  10, 9,  11, 8
        .byte  14, 13, 15, 12
endconst

// convert rgb to 16-bit y, u, or v
// uses v3 and v4

.macro rgbconv16 dst, b, g, r, bc, gc, rc, shr_bits
        smull           v3.4s, \b\().4h, \bc
        smlal           v3.4s, \g\().4h, \gc
        smlal           v3.4s, \r\().4h, \rc
        smull2          v4.4s, \b\().8h, \bc
        smlal2          v4.4s, \g\().8h, \gc
        smlal2          v4.4s, \r\().8h, \rc        // v3:v4 = b * bc + g * gc + r * rc (32-bit)
        shrn            \dst\().4h, v3.4s, \shr_bits
        shrn2           \dst\().8h, v4.4s, \shr_bits       // dst = b * bc + g * gc + r * rc (16-bit)
.endm

// void ff_rgb24toyv12_neon(const uint8_t *src, uint8_t *ydst, uint8_t *udst,
//                          uint8_t *vdst, int width, int height, int lumStride,
//                          int chromStride, int srcStride, int32_t *rgb2yuv);
function ff_rgb24toyv12_neon, export=1
// x0  const uint8_t *src
// x1  uint8_t *ydst
// x2  uint8_t *udst
// x3  uint8_t *vdst
// w4  int width
// w5  int height
// w6  int lumStride
// w7  int chromStride
        ldrsw           x14, [sp]
        ldr             x15, [sp, #8]
// x14 int srcStride
// x15 int32_t *rgb2yuv

        // extend width and stride parameters
        uxtw            x4, w4
        sxtw            x6, w6
        sxtw            x7, w7

        // src1 = x0
        // src2 = x10
        add             x10, x0,  x14               // x10 = src + srcStride
        lsl             x14, x14, #1                // srcStride *= 2
        add             x11, x4,  x4, lsl #1        // x11 = 3 * width
        sub             x14, x14, x11               // srcPadding = (2 * srcStride) - (3 * width)

        // ydst1 = x1
        // ydst2 = x11
        add             x11, x1,  x6                // x11 = ydst + lumStride
        lsl             x6,  x6,  #1                // lumStride *= 2
        sub             x6,  x6,  x4                // lumPadding = (2 * lumStride) - width

        sub             x7,  x7,  x4, lsr #1        // chromPadding = chromStride - (width / 2)

        // load rgb2yuv coefficients into v0, v1, and v2
        add             x15, x15, #RGB2YUV_COEFFS
        ld1             {v0.8h-v2.8h}, [x15]        // load 24 values

        // load offset constants
        movi            Y_OFFSET.8h,  #0x10, lsl #8
        movi            UV_OFFSET.8h, #0x80, lsl #8

1:
        mov             w15, w4                     // w15 = width

2:
        // load first line
        ld3             {v26.16b, v27.16b, v28.16b}, [x0], #48

        // widen first line to 16-bit
        uxtl            v16.8h, v26.8b              // v16 = B11
        uxtl            v17.8h, v27.8b              // v17 = G11
        uxtl            v18.8h, v28.8b              // v18 = R11
        uxtl2           v19.8h, v26.16b             // v19 = B12
        uxtl2           v20.8h, v27.16b             // v20 = G12
        uxtl2           v21.8h, v28.16b             // v21 = R12

        // calculate Y values for first line
        rgbconv16       v24, v16, v17, v18, BY, GY, RY, #7 // v24 = Y11
        rgbconv16       v25, v19, v20, v21, BY, GY, RY, #7 // v25 = Y12

        // load second line
        ld3             {v26.16b, v27.16b, v28.16b}, [x10], #48

        // pairwise add and save rgb values to calculate average
        addp            v5.8h, v16.8h, v19.8h
        addp            v6.8h, v17.8h, v20.8h
        addp            v7.8h, v18.8h, v21.8h

        // widen second line to 16-bit
        uxtl            v16.8h, v26.8b              // v16 = B21
        uxtl            v17.8h, v27.8b              // v17 = G21
        uxtl            v18.8h, v28.8b              // v18 = R21
        uxtl2           v19.8h, v26.16b             // v19 = B22
        uxtl2           v20.8h, v27.16b             // v20 = G22
        uxtl2           v21.8h, v28.16b             // v21 = R22

        // calculate Y values for second line
        rgbconv16       v26, v16, v17, v18, BY, GY, RY, #7 // v26 = Y21
        rgbconv16       v27, v19, v20, v21, BY, GY, RY, #7 // v27 = Y22

        // pairwise add rgb values to calculate average
        addp            v16.8h, v16.8h, v19.8h
        addp            v17.8h, v17.8h, v20.8h
        addp            v18.8h, v18.8h, v21.8h

        // calculate sum of r, g, b components in 2x2 blocks
        add             v16.8h, v16.8h, v5.8h
        add             v17.8h, v17.8h, v6.8h
        add             v18.8h, v18.8h, v7.8h

        // calculate U and V values
        rgbconv16       v28, v16, v17, v18, BU, GU, RU, #9 // v28 = U
        rgbconv16       v29, v16, v17, v18, BV, GV, RV, #9 // v29 = V

        // add offsets and narrow all values
        addhn           v24.8b, v24.8h, Y_OFFSET.8h
        addhn           v25.8b, v25.8h, Y_OFFSET.8h
        addhn           v26.8b, v26.8h, Y_OFFSET.8h
        addhn           v27.8b, v27.8h, Y_OFFSET.8h
        addhn           v28.8b, v28.8h, UV_OFFSET.8h
        addhn           v29.8b, v29.8h, UV_OFFSET.8h

        subs            w15, w15, #16

        // store output
        st1             {v24.8b, v25.8b}, [x1], #16 // store ydst1
        st1             {v26.8b, v27.8b}, [x11], #16 // store ydst2
        st1             {v28.8b}, [x2], #8          // store udst
        st1             {v29.8b}, [x3], #8          // store vdst

        b.gt            2b

        subs            w5,  w5,  #2

        // row += 2
        add             x0,  x0,  x14               // src1  += srcPadding
        add             x10, x10, x14               // src2  += srcPadding
        add             x1,  x1,  x6                // ydst1 += lumPadding
        add             x11, x11, x6                // ydst2 += lumPadding
        add             x2,  x2,  x7                // udst  += chromPadding
        add             x3,  x3,  x7                // vdst  += chromPadding
        b.gt            1b

        ret
endfunc

// void ff_interleave_bytes_neon(const uint8_t *src1, const uint8_t *src2,
//                               uint8_t *dest, int width, int height,
//                               int src1Stride, int src2Stride, int dstStride);
function ff_interleave_bytes_neon, export=1
        sub             w5,  w5,  w3
        sub             w6,  w6,  w3
        sub             w7,  w7,  w3, lsl #1
1:
        ands            w8,  w3,  #0xfffffff0 // & ~15
        b.eq            3f
2:
        ld1             {v0.16b}, [x0], #16
        ld1             {v1.16b}, [x1], #16
        subs            w8,  w8,  #16
        st2             {v0.16b, v1.16b}, [x2], #32
        b.gt            2b

        tst             w3,  #15
        b.eq            9f

3:
        tst             w3,  #8
        b.eq            4f
        ld1             {v0.8b}, [x0], #8
        ld1             {v1.8b}, [x1], #8
        st2             {v0.8b, v1.8b}, [x2], #16
4:
        tst             w3,  #4
        b.eq            5f

        ld1             {v0.s}[0], [x0], #4
        ld1             {v1.s}[0], [x1], #4
        zip1            v0.8b,   v0.8b,   v1.8b
        st1             {v0.8b}, [x2], #8

5:
        ands            w8,  w3,  #3
        b.eq            9f
6:
        ldrb            w9,  [x0], #1
        ldrb            w10, [x1], #1
        subs            w8,  w8,  #1
        bfi             w9,  w10, #8,  #8
        strh            w9,  [x2], #2
        b.gt            6b

9:
        subs            w4,  w4,  #1
        b.eq            0f
        add             x0,  x0,  w5, sxtw
        add             x1,  x1,  w6, sxtw
        add             x2,  x2,  w7, sxtw
        b               1b

0:
        ret
endfunc

// void ff_deinterleave_bytes_neon(const uint8_t *src, uint8_t *dst1, uint8_t *dst2,
//                                 int width, int height, int srcStride,
//                                 int dst1Stride, int dst2Stride);
function ff_deinterleave_bytes_neon, export=1
        sub             w5,  w5,  w3, lsl #1
        sub             w6,  w6,  w3
        sub             w7,  w7,  w3
1:
        ands            w8,  w3,  #0xfffffff0 // & ~15
        b.eq            3f
2:
        ld2             {v0.16b, v1.16b}, [x0], #32
        subs            w8,  w8,  #16
        st1             {v0.16b}, [x1], #16
        st1             {v1.16b}, [x2], #16
        b.gt            2b

        tst             w3,  #15
        b.eq            9f

3:
        tst             w3,  #8
        b.eq            4f
        ld2             {v0.8b, v1.8b}, [x0], #16
        st1             {v0.8b}, [x1], #8
        st1             {v1.8b}, [x2], #8
4:
        tst             w3,  #4
        b.eq            5f

        ld1             {v0.8b}, [x0], #8
        shrn            v1.8b,   v0.8h, #8
        xtn             v0.8b,   v0.8h
        st1             {v0.s}[0], [x1], #4
        st1             {v1.s}[0], [x2], #4

5:
        ands            w8,  w3,  #3
        b.eq            9f
6:
        ldrh            w9,  [x0], #2
        subs            w8,  w8,  #1
        ubfx            w10, w9,  #8,  #8
        strb            w9,  [x1], #1
        strb            w10, [x2], #1
        b.gt            6b

9:
        subs            w4,  w4,  #1
        b.eq            0f
        add             x0,  x0,  w5, sxtw
        add             x1,  x1,  w6, sxtw
        add             x2,  x2,  w7, sxtw
        b               1b

0:
        ret
endfunc

.macro neon_shuf shuf
function ff_shuffle_bytes_\shuf\()_neon, export=1
        movrel          x9, shuf_\shuf\()_tbl
        ld1             {v1.16b}, [x9]
        and             w5, w2, #~15
        and             w3, w2, #8
        and             w4, w2, #4
        cbz             w5, 2f
1:
        ld1             {v0.16b}, [x0], #16
        subs            w5, w5, #16
        tbl             v0.16b, {v0.16b}, v1.16b
        st1             {v0.16b}, [x1], #16
        b.gt            1b
2:
        cbz             w3, 3f
        ld1             {v0.8b}, [x0], #8
        tbl             v0.8b, {v0.16b}, v1.8b
        st1             {v0.8b}, [x1], #8
3:
        cbz             w4, 4f
.if \shuf == 0321
        ldr             w5, [x0]
        rev             w5, w5
        ror             w5, w5, #24
        str             w5, [x1]
.endif
.if \shuf == 1230
        ldr             w5, [x0]
        ror             w5, w5, #8
        str             w5, [x1]
.endif
.if \shuf == 2103
        ldr             w5, [x0]
        rev             w5, w5
        ror             w5, w5, #8
        str             w5, [x1]
.endif
.if \shuf == 3012
        ldr             w5, [x0]
        ror             w5, w5, #24
        str             w5, [x1]
.endif
.if \shuf == 3210
        ldr             w5, [x0]
        rev             w5, w5
        str             w5, [x1]
.endif
.if \shuf == 3102 || \shuf == 2013 || \shuf == 1203 || \shuf == 2130
        ld1             {v0.s}[0], [x0]
        tbl             v0.8b, {v0.16b}, v1.8b
        st1             {v0.s}[0], [x1]
.endif
4:
        ret
endfunc
.endm

neon_shuf 0321
neon_shuf 1230
neon_shuf 2103
neon_shuf 3012
neon_shuf 3102
neon_shuf 2013
neon_shuf 1203
neon_shuf 2130
neon_shuf 3210

/*
v0-v7 - two consecutive lines
x0 - upper Y destination
x1 - U destination
x2 - V destination
x3 - upper src line
w5 - width/iteration counter - count of line pairs for yuv420, of single lines for 422
x6 - lum padding
x7 - chrom padding
x8 - src padding
w9 - number of bytes remaining in the tail
x10 - lower Y destination
w12 - tmp
x13 - lower src line
w14 - tmp
w17 - set to 1 if last line has to be handled separately (odd height)
*/

// one fast path iteration processes 16 uyvy tuples
// is_line_tail is set to 1 when final 16 tuples are being processed
// skip_storing_chroma is set to 1 when final line is processed and the height is odd
.macro fastpath_iteration src_fmt, dst_fmt, is_line_tail, skip_storing_chroma
        ld4             {v0.16b - v3.16b}, [x3], #64
.if ! \is_line_tail
        subs            w14, w14, #32
.endif

.if ! \skip_storing_chroma
.ifc \dst_fmt, yuv420
        ld4             {v4.16b - v7.16b}, [x13], #64
.endif

.ifc \dst_fmt, yuv420                                    // store UV
.ifc \src_fmt, uyvy
        uhadd           v0.16b, v4.16b, v0.16b            // halving sum of U
        uhadd           v2.16b, v6.16b, v2.16b            // halving sum of V
.else
        uhadd           v1.16b, v5.16b, v1.16b            // halving sum of U
        uhadd           v3.16b, v7.16b, v3.16b            // halving sum of V
.endif
.endif

.ifc \src_fmt, uyvy
        st1             {v2.16b}, [x2], #16
        st1             {v0.16b}, [x1], #16
.else
        st1             {v3.16b}, [x2], #16
        st1             {v1.16b}, [x1], #16
.endif

.ifc \dst_fmt, yuv420                                    // store_y
.ifc \src_fmt, uyvy
        mov             v6.16b, v5.16b
        st2             {v6.16b,v7.16b}, [x10], #32
.else
        mov             v5.16b, v4.16b
        st2             {v5.16b,v6.16b}, [x10], #32
.endif
.endif

.endif // ! \skip_storing_chroma

.ifc \src_fmt, uyvy
        mov             v2.16b, v1.16b
        st2             {v2.16b,v3.16b}, [x0], #32
.else
        mov             v1.16b, v0.16b
        st2             {v1.16b,v2.16b}, [x0], #32
.endif
.endm

// shift pointers back to width - 32 to process the tail of the line
// if the height is odd, processing the final line is simplified
.macro fastpath_shift_back_pointers src_fmt, dst_fmt, is_final_odd_line
        add             x3, x3, w9, sxtw #1
        sub             x3, x3, #64
.if ! \is_final_odd_line
.ifc \dst_fmt, yuv420
        add             x13, x13, w9, sxtw #1
        sub             x13, x13, #64
        add             x10, x10, w9, sxtw
        sub             x10, x10, #32
.endif
.endif
        add             x0, x0, w9, sxtw
        sub             x0, x0, #32
.if ! \is_final_odd_line
        asr             w14, w9, #1
        add             x1, x1, w14, sxtw
        sub             x1, x1, #16
        add             x2, x2, w14, sxtw
        sub             x2, x2, #16
.endif
.endm

.macro slowpath_iteration src_fmt, dst_fmt, skip_storing_chroma
.ifc \dst_fmt, yuv422
.ifc \src_fmt, uyvy
        ldrb            w12, [x3], #1
        ldrb            w14, [x3], #1
        strb            w12, [x1], #1
        strb            w14, [x0], #1
        ldrb            w12, [x3], #1
        ldrb            w14, [x3], #1
        strb            w12, [x2], #1
        strb            w14, [x0], #1
.else
        ldrb            w12, [x3], #1
        ldrb            w14, [x3], #1
        strb            w12, [x0], #1
        strb            w14, [x1], #1
        ldrb            w12, [x3], #1
        ldrb            w14, [x3], #1
        strb            w12, [x0], #1
        strb            w14, [x2], #1
.endif
.endif
.ifc \dst_fmt, yuv420
.ifc \src_fmt, uyvy
.if \skip_storing_chroma
        ldrb            w12, [x3], #2
        ldrb            w14, [x3], #2
        strb            w12, [x0], #1
        strb            w14, [x0], #1
.else
        ldrb            w12, [x3], #1
        ldrb            w14, [x13], #1
        add             w12, w12, w14
        lsr             w12, w12, #1
        strb            w12, [x1], #1
        ldrb            w14, [x3], #1
        ldrb            w12, [x13], #1
        strb            w14, [x0], #1
        strb            w12, [x10], #1
        ldrb            w14, [x13], #1
        ldrb            w12, [x3], #1
        add             w12, w12, w14
        lsr             w12, w12, #1
        strb            w12, [x2], #1
        ldrb            w14, [x3], #1
        ldrb            w12, [x13], #1
        strb            w14, [x0], #1
        strb            w12, [x10], #1
.endif
.else
.if \skip_storing_chroma
        ldrb            w12, [x3], #2
        ldrb            w14, [x3], #2
        strb            w12, [x0], #1
        strb            w14, [x0], #1
.else
        ldrb            w12, [x3], #1
        ldrb            w14, [x13], #1
        strb            w12, [x0], #1
        strb            w14, [x10], #1
        ldrb            w12, [x3], #1
        ldrb            w14, [x13], #1
        add             w12, w12, w14
        lsr             w12, w12, #1
        strb            w12, [x1], #1
        ldrb            w14, [x3], #1
        ldrb            w12, [x13], #1
        strb            w14, [x0], #1
        strb            w12, [x10], #1
        ldrb            w14, [x13], #1
        ldrb            w12, [x3], #1
        add             w12, w12, w14
        lsr             w12, w12, #1
        strb            w12, [x2], #1
.endif
.endif
.endif
.endm

.macro move_pointers_to_next_line src_fmt, dst_fmt, is_final_odd_line
        add             x3, x3, x8
        add             x0, x0, x6
.ifc \dst_fmt, yuv420
        add             x13, x13, x8
        add             x10, x10, x6
.endif
        add             x1, x1, x7
        add             x2, x2, x7
.endm

.macro interleaved_yuv_to_planar src_fmt, dst_fmt
function ff_\src_fmt\()to\dst_fmt\()_neon, export=1
        sxtw            x6, w6
        sxtw            x7, w7
        ldrsw           x8, [sp]
        ands            w11, w4, #~31                     // choose between fast and slow path

.ifc \dst_fmt, yuv420
        add             x10, x0, x6
        add             x13, x3, x8
        add             x8, x8, x8
        add             x6, x6, x6
        and             w17, w5, #1
        asr             w5, w5, #1
.endif
        asr             w9, w4, #1
        sub             x8, x8, w4, sxtw #1               // src offset
        sub             x6, x6, w4, sxtw                  // lum offset
        sub             x7, x7, x9                        // chr offset

        b.eq            6f

1:                                                        // fast path - the width is at least 32
        and             w14, w4, #~31                     // w14 is the main loop counter
        and             w9, w4, #31                       // w9 holds the remaining width, 0 to 31
2:
        fastpath_iteration \src_fmt, \dst_fmt, 0, 0
        b.ne            2b
        fastpath_shift_back_pointers \src_fmt, \dst_fmt, 0
        fastpath_iteration \src_fmt, \dst_fmt, 0, 0
        subs            w5, w5, #1
        move_pointers_to_next_line \src_fmt, \dst_fmt
        b.ne            1b

.ifc \dst_fmt, yuv420                                    // handle the last line in case the height is odd
        cbz             w17, 3f
        and             w14, w4, #~31
4:
        fastpath_iteration \src_fmt, \dst_fmt, 0, 1
        b.ne            4b
        fastpath_shift_back_pointers \src_fmt, \dst_fmt, 1
        fastpath_iteration \src_fmt, \dst_fmt, 1, 1
3:
.endif
        ret

6:                                                        // slow path - width is at most 31
        and             w9, w4, #31
7:
        subs            w9, w9, #2
        slowpath_iteration \src_fmt, \dst_fmt, 0
        b.ne            7b
        subs            w5, w5, #1
        move_pointers_to_next_line \src_fmt, \dst_fmt
        b.ne            6b

.ifc \dst_fmt, yuv420
        cbz             w17, 8f
        and             w9, w4, #31
.ifc \src_fmt, uyvy
        add             x3, x3, #1
.endif
5:
        subs            w9, w9, #2
        slowpath_iteration \src_fmt, \dst_fmt, 1
        b.ne            5b
8:
.endif
        ret
endfunc
.endm

interleaved_yuv_to_planar uyvy, yuv422
interleaved_yuv_to_planar uyvy, yuv420
interleaved_yuv_to_planar yuyv, yuv422
interleaved_yuv_to_planar yuyv, yuv420

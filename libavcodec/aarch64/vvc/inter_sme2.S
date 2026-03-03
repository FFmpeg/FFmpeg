/*
 * Copyright (c) 2025 Georgii Zagoruiko <george.zaguri@gmail.com>
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

#define VVC_MAX_PB_SIZE 128

ENABLE_SME2

#if HAVE_SME_I16I64
ENABLE_SME_I16I64
.macro first_group_filter_luma_offsets breg, shift
        // x20-x23: p5[0],p3[-1],p1[0],p0[3]
        // x24-x27: p6[0],p4[1],p2[0],p0[-3]
        neg             x26, x11
        ubfx            x20, \breg, #(3+\shift), #2
        ubfx            x21, \breg, #(1+\shift), #2
        mul             x24, x20, x26
        mul             x20, x20, x11
        mul             x25, x21, x26
        mul             x21, x21, x11
        ubfx            x22, \breg, #(\shift), #1
        sub             x21, x21, #1
        mul             x26, x22, x26
        mul             x22, x22, x11
        mov             x23, #3
        add             x25, x25, #1
        mov             x27, #-3
.endm

.macro second_group_filter_luma_offsets breg, shift
        // x20-x23: p3[ 1],p1[ 2],p1[-1],p0[ 2]
        // x24-x27: p4[-1],p2[-2],p2[ 1],p0[-2]
        neg             x26, x11
        ubfx            x20, \breg, #(1+\shift), #2
        ubfx            x21, \breg, #(\shift), #1
        mul             x24, x20, x26
        mul             x20, x20, x11
        mul             x25, x21, x26
        mul             x26, x21, x26
        mul             x21, x21, x11
        add             x20, x20, #1
        sub             x22, x21, #1
        add             x21, x21, #2
        mov             x23, #2
        sub             x24, x24, #1
        sub             x25, x25, #2
        add             x26, x26, #1
        mov             x27, #-2
.endm

.macro third_group_filter_luma_offsets breg, shift
        // x20-x23: p3[0],p1[ 1],p1[-2],p0[ 1]
        // x24-x27: p4[0],p2[-1],p2[ 2],p0[-1]
        neg             x26, x11
        ubfx            x21, \breg, #(\shift), #1
        ubfx            x20, \breg, #(1+\shift), #2
        mul             x25, x21, x26
        mul             x26, x21, x26
        mul             x21, x21, x11
        mul             x24, x20, x26
        mul             x20, x20, x11
        sub             x22, x21, #2
        add             x21, x21, #1
        mov             x23, #1
        sub             x25, x25, #1
        mov             x27, #-1
        add             x26, x26, #2
.endm

.macro kernel_filter_luma_8_sme2 src, zreg, idx
        ld1b            z20.h, p0/z, [\src, x20]
        ld1b            z21.h, p0/z, [\src, x21]
        ld1b            z22.h, p0/z, [\src, x22]
        ld1b            z23.h, p0/z, [\src, x23]
        ld1b            z24.h, p0/z, [\src, x24]
        ld1b            z25.h, p0/z, [\src, x25]
        neg             z8.h, p0/m, \zreg // -p0
        ld1b            z26.h, p0/z, [\src, x26]
        ld1b            z27.h, p0/z, [\src, x27]
        add             {z20.h-z23.h}, {z20.h-z23.h}, z8.h
        add             {z24.h-z27.h}, {z24.h-z27.h}, z8.h
        // transpose data vectors
        zip             {z20.h-z23.h}, {z20.h-z23.h}
        zip             {z24.h-z27.h}, {z24.h-z27.h}
        // clip data
        sclamp          z20.h, z16.h, z12.h
        sclamp          z24.h, z16.h, z12.h
        sclamp          z21.h, z17.h, z13.h
        sclamp          z25.h, z17.h, z13.h
        sclamp          z22.h, z18.h, z14.h
        sclamp          z26.h, z18.h, z14.h
        sclamp          z23.h, z19.h, z15.h
        sclamp          z27.h, z19.h, z15.h
        sdot            za.d[w10, \idx], {z20.h-z23.h}, {z28.h-z31.h}
        sdot            za.d[w10, \idx], {z24.h-z27.h}, {z28.h-z31.h}
.endm

function ff_vvc_alf_filter_luma_8_sme2, export=1
        // dst           .req x0
        // src           .req x1
        // strides       .req x2
        // dims          .req x3
        // filter        .req x4
        // clip          .req x5
        // vb            .req x6
        sme_entry
        stp             x29, x30, [sp, #-96]!
        mov             x29, sp
        stp             x19, x20, [sp, #16]
        stp             x21, x22, [sp, #32]
        stp             x23, x24, [sp, #48]
        stp             x25, x26, [sp, #64]
        stp             x27, x28, [sp, #80]

        lsr             x7, x3, #32
        cnth            x11
        mov             w8, w3
        sub             w9, w8, #1
        sdiv            w9, w9, w11
        msub            w9, w9, w11, w8
        whilelo         p10.h, xzr, x9
        ptrue           p1.h
        lsr             x11, x2, #32 // src stride
        lsr             w2, w2, #0 // leave dst stride only
        mov             w10, #0
        mov             w12, #255
        dup             z9.h, w10
        dup             z10.h, w12
1:
        lsr             x20, x3, #32
        mov             p0.b, p10.b
        sub             w20, w20, w7
        mov             w12, w9
        sub             w6, w6, #6
        // offsets are packed into the format: (M<<3)|(N<<1)|K, where M is p5/p6 offset (multiply), N is p3/p4 offset, K is p1/p2 offset
        mov             w21, #0
        mov             w22, #0xB
        mov             w23, #0x15
        mov             w13, #0x1D // 0x1D == (3<<3)|(2<<1)|1
        mov             w14, #0x1D
        mov             w15, #0x1D
        mov             w16, #0x1D
        // y == vb_pos - 6
        cmp             w20, w6
        add             w6, w6, #1
        csel            w16, w16, w23, ne
        // y == vb_pos - 5
        cmp             w20, w6
        add             w6, w6, #1
        csel            w15, w15, w23, ne
        csel            w16, w16, w22, ne
        // y == vb_pos - 4
        cmp             w20, w6
        add             w6, w6, #1
        csel            w14, w14, w23, ne
        csel            w15, w15, w22, ne
        csel            w16, w16, w21, ne
        // y == vb_pos - 3
        cmp             w20, w6
        add             w6, w6, #1
        csel            w13, w13, w23, ne
        csel            w14, w14, w22, ne
        csel            w15, w15, w21, ne
        csel            w16, w16, w21, ne
        // y == vb_pos - 2
        cmp             w20, w6
        add             w6, w6, #1
        csel            w13, w13, w22, ne
        csel            w14, w14, w21, ne
        csel            w15, w15, w21, ne
        csel            w16, w16, w22, ne
        // y == vb_pos - 1
        cmp             w20, w6
        add             w6, w6, #1
        csel            w13, w13, w21, ne
        csel            w14, w14, w21, ne
        csel            w15, w15, w22, ne
        csel            w16, w16, w23, ne
        // y == vb_pos
        cmp             w20, w6
        add             w6, w6, #1
        csel            w13, w13, w21, ne
        csel            w14, w14, w22, ne
        csel            w15, w15, w23, ne
        // y == vb_pos + 1
        cmp             w20, w6
        add             w6, w6, #1
        csel            w13, w13, w22, ne
        csel            w14, w14, w23, ne
        // y == vb_pos + 2
        cmp             w20, w6
        sub             w6, w6, #2
        csel            w13, w13, w23, ne
        orr             w13, w13, w14, lsl #8
        orr             w13, w13, w15, lsl #16
        orr             w13, w13, w16, lsl #24
        mov             x14, x1
        mov             x19, x0
2:
        // Load clip [12=>3x4 memory layout]
        ld3h            {z0.h-z2.h}, p0/z, [x5]
        // Load filter [12=>3x4 memory layout]
        ld3h            {z3.h-z5.h}, p0/z, [x4]
        add             x15, x14, x11
        add             x16, x14, x11, lsl #1
        add             x17, x15, x11, lsl #1
        add             x30, x19, x2, lsl #1

        mov             z12.d, z0.d
        mov             z13.d, z0.d
        mov             z14.d, z0.d
        mov             z15.d, z0.d
        // copy filter into 4 vectors and then zip
        mov             z28.d, z3.d
        mov             z29.d, z3.d
        zip             {z12.d-z15.d}, {z12.d-z15.d}
        mov             z30.d, z3.d
        mov             z31.d, z3.d
        neg             z16.h, p1/m, z12.h
        neg             z17.h, p1/m, z13.h
        neg             z18.h, p1/m, z14.h
        neg             z19.h, p1/m, z15.h
        zip             {z28.d-z31.d}, {z28.d-z31.d}
        // p0 (curr)
        ld1b            z6.h, p0/z, [x14]
        ld1b            z7.h, p0/z, [x15]
        ld1b            z0.h, p0/z, [x16]
        ld1b            z3.h, p0/z, [x17]
        // clip & filter (first group): a0,a3,a6,a9, a12...
        // {p5[0],p3[-1],p1[0],p0[3]} -> left operand in clip
        // {p6[0],p4[1],p2[0],p0[-3]} -> right operand in clip
        first_group_filter_luma_offsets x13, 0
        kernel_filter_luma_8_sme2 x14, z6.h, 0
        first_group_filter_luma_offsets x13, 8
        kernel_filter_luma_8_sme2 x15, z7.h, 1
        first_group_filter_luma_offsets x13, 16
        kernel_filter_luma_8_sme2 x16, z0.h, 2
        first_group_filter_luma_offsets x13, 24
        kernel_filter_luma_8_sme2 x17, z3.h, 3

        mov             z12.d, z1.d
        mov             z13.d, z1.d
        mov             z14.d, z1.d
        mov             z15.d, z1.d
        // copy filter into 4 vectors and then zip
        mov             z28.d, z4.d
        mov             z29.d, z4.d
        zip             {z12.d-z15.d}, {z12.d-z15.d}
        mov             z30.d, z4.d
        mov             z31.d, z4.d
        // -clip
        neg             z16.h, p1/m, z12.h
        neg             z17.h, p1/m, z13.h
        neg             z18.h, p1/m, z14.h
        neg             z19.h, p1/m, z15.h
        zip             {z28.d-z31.d}, {z28.d-z31.d}
        // clip & filter (second group): a1,a4,a7,a10,a13...
        // left:  {p3[ 1],p1[ 2],p1[-1],p0[ 2]}
        // right: {p4[-1],p2[-2],p2[ 1],p0[-2]}
        second_group_filter_luma_offsets x13, 0
        kernel_filter_luma_8_sme2 x14, z6.h, 0
        second_group_filter_luma_offsets x13, 8
        kernel_filter_luma_8_sme2 x15, z7.h, 1
        second_group_filter_luma_offsets x13, 16
        kernel_filter_luma_8_sme2 x16, z0.h, 2
        second_group_filter_luma_offsets x13, 24
        kernel_filter_luma_8_sme2 x17, z3.h, 3

        mov             z12.d, z2.d
        mov             z13.d, z2.d
        mov             z14.d, z2.d
        mov             z15.d, z2.d
        // copy filter into 4 vectors and then zip
        mov             z28.d, z5.d
        mov             z29.d, z5.d
        zip             {z12.d-z15.d}, {z12.d-z15.d}
        mov             z30.d, z5.d
        mov             z31.d, z5.d
        // -clip
        neg             z16.h, p1/m, z12.h
        neg             z17.h, p1/m, z13.h
        neg             z18.h, p1/m, z14.h
        neg             z19.h, p1/m, z15.h
        zip             {z28.d-z31.d}, {z28.d-z31.d}
        // clip & filter (third group): a2,a5,a8,a11,a14...
        // left:  {p3[0],p1[ 1],p1[-2],p0[ 1]}
        // right: {p4[0],p2[-1],p2[ 2],p0[-1]}
        third_group_filter_luma_offsets x13, 0
        kernel_filter_luma_8_sme2 x14, z6.h, 0
        third_group_filter_luma_offsets x13, 8
        kernel_filter_luma_8_sme2 x15, z7.h, 1
        third_group_filter_luma_offsets x13, 16
        kernel_filter_luma_8_sme2 x16, z0.h, 2
        third_group_filter_luma_offsets x13, 24
        kernel_filter_luma_8_sme2 x17, z3.h, 3
        mova            {z16.d-z19.d}, za.d[w10, 0]
        mova            {z20.d-z23.d}, za.d[w10, 1]
        mova            {z24.d-z27.d}, za.d[w10, 2]
        mova            {z28.d-z31.d}, za.d[w10, 3]
        sqrshr          z12.h, {z16.d-z19.d}, #7
        sqrshr          z13.h, {z20.d-z23.d}, #7
        sqrshr          z14.h, {z24.d-z27.d}, #7
        sqrshr          z15.h, {z28.d-z31.d}, #7
        tbnz            x13, #0, 10f
        sqrshr          z12.h, {z16.d-z19.d}, #10
10:
        tbnz            x13, #8, 11f
        sqrshr          z13.h, {z20.d-z23.d}, #10
11:
        tbnz            x13, #16, 12f
        sqrshr          z14.h, {z24.d-z27.d}, #10
12:
        tbnz            x13, #24, 13f
        sqrshr          z15.h, {z28.d-z31.d}, #10
13:
        add             z12.h, z12.h, z6.h
        add             z13.h, z13.h, z7.h
        add             z14.h, z14.h, z0.h
        add             z15.h, z15.h, z3.h
        sclamp          {z12.h-z15.h}, z9.h, z10.h
        st1b            z12.h, p0, [x19]
        st1b            z13.h, p0, [x19, x2]
        st1b            z14.h, p0, [x30]
        st1b            z15.h, p0, [x30, x2]
        zero            {za}
        add             x14, x14, x12
        add             x19, x19, x12
        ptrue           p0.h
        subs            w8, w8, w12
        add             w12, w12, w12, lsl #1
        add             x4, x4, x12, lsl #1
        add             x5, x5, x12, lsl #1
        cnth            x12
        b.gt            2b
        mov             w8, w3
        subs            w7, w7, #4
        add             x1, x1, x11, lsl #2
        add             x0, x0, x2, lsl #2
        b.gt            1b

        ldp             x19, x20, [sp, #16]
        ldp             x21, x22, [sp, #32]
        ldp             x23, x24, [sp, #48]
        ldp             x25, x26, [sp, #64]
        ldp             x27, x28, [sp, #80]
        ldp             x29, x30, [sp], #96
        sme_exit
        ret
endfunc

.macro kernel_filter_luma_16_sme2 src, zreg, idx
        ld1h            z20.h, p0/z, [\src, x20, lsl #1]
        ld1h            z21.h, p0/z, [\src, x21, lsl #1]
        ld1h            z22.h, p0/z, [\src, x22, lsl #1]
        ld1h            z23.h, p0/z, [\src, x23, lsl #1]
        ld1h            z24.h, p0/z, [\src, x24, lsl #1]
        ld1h            z25.h, p0/z, [\src, x25, lsl #1]
        neg             z8.h, p0/m, \zreg // -p0
        ld1h            z26.h, p0/z, [\src, x26, lsl #1]
        ld1h            z27.h, p0/z, [\src, x27, lsl #1]
        add             {z20.h-z23.h}, {z20.h-z23.h}, z8.h
        add             {z24.h-z27.h}, {z24.h-z27.h}, z8.h
        // transpose data vectors
        zip             {z20.h-z23.h}, {z20.h-z23.h}
        zip             {z24.h-z27.h}, {z24.h-z27.h}
        // clip data
        sclamp          z20.h, z16.h, z12.h
        sclamp          z24.h, z16.h, z12.h
        sclamp          z21.h, z17.h, z13.h
        sclamp          z25.h, z17.h, z13.h
        sclamp          z22.h, z18.h, z14.h
        sclamp          z26.h, z18.h, z14.h
        sclamp          z23.h, z19.h, z15.h
        sclamp          z27.h, z19.h, z15.h
        sdot            za.d[w10, \idx], {z20.h-z23.h}, {z28.h-z31.h}
        sdot            za.d[w10, \idx], {z24.h-z27.h}, {z28.h-z31.h}
.endm

function ff_vvc_alf_filter_luma_12_sme2, export=1
        mov             w12, #4095
        b               0f
endfunc

function ff_vvc_alf_filter_luma_10_sme2, export=1
        // dst           .req x0
        // src           .req x1
        // strides       .req x2
        // dims          .req x3
        // filter        .req x4
        // clip          .req x5
        // vb            .req x6
        mov             w12, #1023
0:
        sme_entry
        stp             x29, x30, [sp, #-96]!
        mov             x29, sp
        stp             x19, x20, [sp, #16]
        stp             x21, x22, [sp, #32]
        stp             x23, x24, [sp, #48]
        stp             x25, x26, [sp, #64]
        stp             x27, x28, [sp, #80]

        lsr             x7, x3, #32
        cnth            x11
        mov             w8, w3
        sub             w9, w8, #1
        sdiv            w9, w9, w11
        msub            w9, w9, w11, w8
        whilelo         p10.h, xzr, x9
        ptrue           p1.h
        lsr             x11, x2, #33 // src stride
        lsr             w2, w2, #1
        mov             w10, #0
        dup             z9.h, w10
        dup             z10.h, w12
1:
        lsr             x20, x3, #32
        mov             p0.b, p10.b
        sub             w20, w20, w7
        mov             w12, w9
        sub             w6, w6, #6
        // offsets are packed into the format: (M<<3)|(N<<1)|K, where M is p5/p6 offset (multiply), N is p3/p4 offset, K is p1/p2 offset
        mov             w21, #0
        mov             w22, #0xB
        mov             w23, #0x15
        mov             w13, #0x1D // 0x1D == (3<<3)|(2<<1)|1
        mov             w14, #0x1D
        mov             w15, #0x1D
        mov             w16, #0x1D
        // y == vb_pos - 6
        cmp             w20, w6
        add             w6, w6, #1
        csel            w16, w16, w23, ne
        // y == vb_pos - 5
        cmp             w20, w6
        add             w6, w6, #1
        csel            w15, w15, w23, ne
        csel            w16, w16, w22, ne
        // y == vb_pos - 4
        cmp             w20, w6
        add             w6, w6, #1
        csel            w14, w14, w23, ne
        csel            w15, w15, w22, ne
        csel            w16, w16, w21, ne
        // y == vb_pos - 3
        cmp             w20, w6
        add             w6, w6, #1
        csel            w13, w13, w23, ne
        csel            w14, w14, w22, ne
        csel            w15, w15, w21, ne
        csel            w16, w16, w21, ne
        // y == vb_pos - 2
        cmp             w20, w6
        add             w6, w6, #1
        csel            w13, w13, w22, ne
        csel            w14, w14, w21, ne
        csel            w15, w15, w21, ne
        csel            w16, w16, w22, ne
        // y == vb_pos - 1
        cmp             w20, w6
        add             w6, w6, #1
        csel            w13, w13, w21, ne
        csel            w14, w14, w21, ne
        csel            w15, w15, w22, ne
        csel            w16, w16, w23, ne
        // y == vb_pos
        cmp             w20, w6
        add             w6, w6, #1
        csel            w13, w13, w21, ne
        csel            w14, w14, w22, ne
        csel            w15, w15, w23, ne
        // y == vb_pos + 1
        cmp             w20, w6
        add             w6, w6, #1
        csel            w13, w13, w22, ne
        csel            w14, w14, w23, ne
        // y == vb_pos + 2
        cmp             w20, w6
        sub             w6, w6, #2
        csel            w13, w13, w23, ne
        orr             w13, w13, w14, lsl #8
        orr             w13, w13, w15, lsl #16
        orr             w13, w13, w16, lsl #24
        mov             x14, x1
        mov             x19, x0
2:
        // Load clip [12=>3x4 memory layout]
        ld3h            {z0.h-z2.h}, p0/z, [x5]
        // Load filter [12=>3x4 memory layout]
        ld3h            {z3.h-z5.h}, p0/z, [x4]
        add             x15, x14, x11, lsl #1
        add             x16, x14, x11, lsl #2
        add             x17, x15, x11, lsl #2
        add             x30, x19, x2, lsl #2

        mov             z12.d, z0.d
        mov             z13.d, z0.d
        mov             z14.d, z0.d
        mov             z15.d, z0.d
        // copy filter into 4 vectors and then zip
        mov             z28.d, z3.d
        mov             z29.d, z3.d
        zip             {z12.d-z15.d}, {z12.d-z15.d}
        mov             z30.d, z3.d
        mov             z31.d, z3.d
        neg             z16.h, p1/m, z12.h
        neg             z17.h, p1/m, z13.h
        neg             z18.h, p1/m, z14.h
        neg             z19.h, p1/m, z15.h
        zip             {z28.d-z31.d}, {z28.d-z31.d}
        // p0 (curr)
        ld1h            z6.h, p0/z, [x14]
        ld1h            z7.h, p0/z, [x15]
        ld1h            z0.h, p0/z, [x16]
        ld1h            z3.h, p0/z, [x17]
        // clip & filter (first group): a0,a3,a6,a9, a12...
        // {p5[0],p3[-1],p1[0],p0[3]} -> left operand in clip
        // {p6[0],p4[1],p2[0],p0[-3]} -> right operand in clip
        first_group_filter_luma_offsets x13, 0
        kernel_filter_luma_16_sme2 x14, z6.h, 0
        first_group_filter_luma_offsets x13, 8
        kernel_filter_luma_16_sme2 x15, z7.h, 1
        first_group_filter_luma_offsets x13, 16
        kernel_filter_luma_16_sme2 x16, z0.h, 2
        first_group_filter_luma_offsets x13, 24
        kernel_filter_luma_16_sme2 x17, z3.h, 3

        mov             z12.d, z1.d
        mov             z13.d, z1.d
        mov             z14.d, z1.d
        mov             z15.d, z1.d
        // copy filter into 4 vectors and then zip
        mov             z28.d, z4.d
        mov             z29.d, z4.d
        zip             {z12.d-z15.d}, {z12.d-z15.d}
        mov             z30.d, z4.d
        mov             z31.d, z4.d
        // -clip
        neg             z16.h, p1/m, z12.h
        neg             z17.h, p1/m, z13.h
        neg             z18.h, p1/m, z14.h
        neg             z19.h, p1/m, z15.h
        zip             {z28.d-z31.d}, {z28.d-z31.d}
        // clip & filter (second group): a1,a4,a7,a10,a13...
        // left:  {p3[ 1],p1[ 2],p1[-1],p0[ 2]}
        // right: {p4[-1],p2[-2],p2[ 1],p0[-2]}
        second_group_filter_luma_offsets x13, 0
        kernel_filter_luma_16_sme2 x14, z6.h, 0
        second_group_filter_luma_offsets x13, 8
        kernel_filter_luma_16_sme2 x15, z7.h, 1
        second_group_filter_luma_offsets x13, 16
        kernel_filter_luma_16_sme2 x16, z0.h, 2
        second_group_filter_luma_offsets x13, 24
        kernel_filter_luma_16_sme2 x17, z3.h, 3

        mov             z12.d, z2.d
        mov             z13.d, z2.d
        mov             z14.d, z2.d
        mov             z15.d, z2.d
        // copy filter into 4 vectors and then zip
        mov             z28.d, z5.d
        mov             z29.d, z5.d
        zip             {z12.d-z15.d}, {z12.d-z15.d}
        mov             z30.d, z5.d
        mov             z31.d, z5.d
        // -clip
        neg             z16.h, p1/m, z12.h
        neg             z17.h, p1/m, z13.h
        neg             z18.h, p1/m, z14.h
        neg             z19.h, p1/m, z15.h
        zip             {z28.d-z31.d}, {z28.d-z31.d}

        // clip & filter (third group): a2,a5,a8,a11,a14...
        // left:  {p3[0],p1[ 1],p1[-2],p0[ 1]}
        // right: {p4[0],p2[-1],p2[ 2],p0[-1]}
        third_group_filter_luma_offsets x13, 0
        kernel_filter_luma_16_sme2 x14, z6.h, 0
        third_group_filter_luma_offsets x13, 8
        kernel_filter_luma_16_sme2 x15, z7.h, 1
        third_group_filter_luma_offsets x13, 16
        kernel_filter_luma_16_sme2 x16, z0.h, 2
        third_group_filter_luma_offsets x13, 24
        kernel_filter_luma_16_sme2 x17, z3.h, 3
        mova            {z16.d-z19.d}, za.d[w10, 0]
        mova            {z20.d-z23.d}, za.d[w10, 1]
        mova            {z24.d-z27.d}, za.d[w10, 2]
        mova            {z28.d-z31.d}, za.d[w10, 3]
        sqrshr          z12.h, {z16.d-z19.d}, #7
        sqrshr          z13.h, {z20.d-z23.d}, #7
        sqrshr          z14.h, {z24.d-z27.d}, #7
        sqrshr          z15.h, {z28.d-z31.d}, #7
        tbnz            x13, #0, 10f
        sqrshr          z12.h, {z16.d-z19.d}, #10
10:
        tbnz            x13, #8, 11f
        sqrshr          z13.h, {z20.d-z23.d}, #10
11:
        tbnz            x13, #16, 12f
        sqrshr          z14.h, {z24.d-z27.d}, #10
12:
        tbnz            x13, #24, 13f
        sqrshr          z15.h, {z28.d-z31.d}, #10
13:
        add             z12.h, z12.h, z6.h
        add             z13.h, z13.h, z7.h
        add             z14.h, z14.h, z0.h
        add             z15.h, z15.h, z3.h
        sclamp          {z12.h-z15.h}, z9.h, z10.h
        st1h            z12.h, p0, [x19]
        st1h            z13.h, p0, [x19, x2, lsl #1]
        st1h            z14.h, p0, [x30]
        st1h            z15.h, p0, [x30, x2, lsl #1]
        zero            {za}
        add             x14, x14, x12, lsl #1
        add             x19, x19, x12, lsl #1
        ptrue           p0.h
        subs            w8, w8, w12
        add             w12, w12, w12, lsl #1
        add             x4, x4, x12, lsl #1
        add             x5, x5, x12, lsl #1
        cnth            x12
        b.gt            2b
        mov             w8, w3
        subs            w7, w7, #4
        add             x1, x1, x11, lsl #3
        add             x0, x0, x2, lsl #3
        b.gt            1b

        ldp             x19, x20, [sp, #16]
        ldp             x21, x22, [sp, #32]
        ldp             x23, x24, [sp, #48]
        ldp             x25, x26, [sp, #64]
        ldp             x27, x28, [sp, #80]
        ldp             x29, x30, [sp], #96
        sme_exit
        ret
endfunc
DISABLE_SME_I16I64
#endif

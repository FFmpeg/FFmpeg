/*
 * Loongson LSX optimized swscale
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 * Contributed by Lu Wang <wanglu@loongson.cn>
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

#include "libavcodec/loongarch/loongson_asm.S"

/* void planar_rgb_to_y_lsx(uint8_t *_dst, const uint8_t *src[4],
 *                          int width, int32_t *rgb2yuv)
 */
function planar_rgb_to_y_lsx
    ld.d            a5,     a1,    0
    ld.d            a6,     a1,    8
    ld.d            a7,     a1,    16

    ld.w            t1,     a3,    0     // ry
    ld.w            t2,     a3,    4     // gy
    ld.w            t3,     a3,    8     // by
    li.w            t4,     9
    li.w            t5,     524544
    li.w            t7,     4
    li.w            t8,     8
    vldi            vr7,    0
    vreplgr2vr.w    vr1,    t1
    vreplgr2vr.w    vr2,    t2
    vreplgr2vr.w    vr3,    t3
    vreplgr2vr.w    vr4,    t4
    vreplgr2vr.w    vr5,    t5
    bge             a2,     t8,    .WIDTH8
    bge             a2,     t7,    .WIDTH4
    blt             zero,   a2,    .WIDTH
    b               .END

.WIDTH8:
    vld             vr8,    a5,    0
    vld             vr9,    a6,    0
    vld             vr10,   a7,    0
    vilvl.b         vr11,   vr7,   vr8
    vilvl.b         vr12,   vr7,   vr9
    vilvl.b         vr13,   vr7,   vr10
    vilvl.h         vr14,   vr7,   vr11
    vilvl.h         vr15,   vr7,   vr12
    vilvl.h         vr16,   vr7,   vr13
    vilvh.h         vr17,   vr7,   vr11
    vilvh.h         vr18,   vr7,   vr12
    vilvh.h         vr19,   vr7,   vr13
    vmul.w          vr20,   vr1,   vr16
    vmul.w          vr21,   vr1,   vr19
    vmadd.w         vr20,   vr2,   vr14
    vmadd.w         vr20,   vr3,   vr15
    vmadd.w         vr21,   vr2,   vr17
    vmadd.w         vr21,   vr3,   vr18
    vadd.w          vr20,   vr20,  vr5
    vadd.w          vr21,   vr21,  vr5
    vsra.w          vr20,   vr20,  vr4
    vsra.w          vr21,   vr21,  vr4
    vpickev.h       vr20,   vr21,  vr20
    vst             vr20,   a0,    0
    addi.d          a2,     a2,    -8
    addi.d          a5,     a5,    8
    addi.d          a6,     a6,    8
    addi.d          a7,     a7,    8
    addi.d          a0,     a0,    16
    bge             a2,     t8,    .WIDTH8
    bge             a2,     t7,    .WIDTH4
    blt             zero,   a2,    .WIDTH
    b               .END

.WIDTH4:
    vld             vr8,    a5,    0
    vld             vr9,    a6,    0
    vld             vr10,   a7,    0
    vilvl.b         vr11,   vr7,   vr8
    vilvl.b         vr12,   vr7,   vr9
    vilvl.b         vr13,   vr7,   vr10
    vilvl.h         vr14,   vr7,   vr11
    vilvl.h         vr15,   vr7,   vr12
    vilvl.h         vr16,   vr7,   vr13
    vmul.w          vr17,   vr1,   vr16
    vmadd.w         vr17,   vr2,   vr14
    vmadd.w         vr17,   vr3,   vr15
    vadd.w          vr17,   vr17,  vr5
    vsra.w          vr17,   vr17,  vr4
    vpickev.h       vr17,   vr17,  vr17
    vstelm.d        vr17,   a0,    0,    0
    addi.d          a2,     a2,    -4
    addi.d          a5,     a5,    4
    addi.d          a6,     a6,    4
    addi.d          a7,     a7,    4
    addi.d          a0,     a0,    8
    bge             a2,     t7,    .WIDTH4
    blt             zero,   a2,    .WIDTH
    b               .END

.WIDTH:
    ld.bu           t0,     a5,    0
    ld.bu           t4,     a6,    0
    ld.bu           t6,     a7,    0
    mul.w           t8,     t6,    t1
    mul.w           t7,     t0,    t2
    add.w           t8,     t8,    t7
    mul.w           t7,     t4,    t3
    add.w           t8,     t8,    t7
    add.w           t8,     t8,    t5
    srai.w          t8,     t8,    9
    st.h            t8,     a0,    0
    addi.d          a2,     a2,    -1
    addi.d          a5,     a5,    1
    addi.d          a6,     a6,    1
    addi.d          a7,     a7,    1
    addi.d          a0,     a0,    2
    blt             zero,   a2,    .WIDTH
.END:
endfunc

/* void planar_rgb_to_uv_lsx(uint8_t *_dstU, uint8_t *_dstV, const uint8_t *src[4],
 *                           int width, int32_t *rgb2yuv)
 */
function planar_rgb_to_uv_lsx
    addi.d          sp,     sp,    -24
    st.d            s1,     sp,    0
    st.d            s2,     sp,    8
    st.d            s3,     sp,    16

    ld.d            a5,     a2,    0
    ld.d            a6,     a2,    8
    ld.d            a7,     a2,    16
    ld.w            t1,     a4,    12     // ru
    ld.w            t2,     a4,    16     // gu
    ld.w            t3,     a4,    20     // bu
    ld.w            s1,     a4,    24     // rv
    ld.w            s2,     a4,    28     // gv
    ld.w            s3,     a4,    32     // bv
    li.w            t4,     9
    li.w            t5,     4194560
    li.w            t7,     4
    li.w            t8,     8
    vldi            vr0,    0
    vreplgr2vr.w    vr1,    t1
    vreplgr2vr.w    vr2,    t2
    vreplgr2vr.w    vr3,    t3
    vreplgr2vr.w    vr4,    s1
    vreplgr2vr.w    vr5,    s2
    vreplgr2vr.w    vr6,    s3
    vreplgr2vr.w    vr7,    t4
    vreplgr2vr.w    vr8,    t5
    bge             a2,     t8,    .LOOP_WIDTH8
    bge             a2,     t7,    .LOOP_WIDTH4
    blt             zero,   a2,    .LOOP_WIDTH
    b               .LOOP_END

.LOOP_WIDTH8:
    vld             vr9,    a5,    0
    vld             vr10,   a6,    0
    vld             vr11,   a7,    0
    vilvl.b         vr9,    vr0,   vr9
    vilvl.b         vr10,   vr0,   vr10
    vilvl.b         vr11,   vr0,   vr11
    vilvl.h         vr12,   vr0,   vr9
    vilvl.h         vr13,   vr0,   vr10
    vilvl.h         vr14,   vr0,   vr11
    vilvh.h         vr15,   vr0,   vr9
    vilvh.h         vr16,   vr0,   vr10
    vilvh.h         vr17,   vr0,   vr11
    vmul.w          vr18,   vr1,   vr14
    vmul.w          vr19,   vr1,   vr17
    vmul.w          vr20,   vr4,   vr14
    vmul.w          vr21,   vr4,   vr17
    vmadd.w         vr18,   vr2,   vr12
    vmadd.w         vr18,   vr3,   vr13
    vmadd.w         vr19,   vr2,   vr15
    vmadd.w         vr19,   vr3,   vr16
    vmadd.w         vr20,   vr5,   vr12
    vmadd.w         vr20,   vr6,   vr13
    vmadd.w         vr21,   vr5,   vr15
    vmadd.w         vr21,   vr6,   vr16
    vadd.w          vr18,   vr18,  vr8
    vadd.w          vr19,   vr19,  vr8
    vadd.w          vr20,   vr20,  vr8
    vadd.w          vr21,   vr21,  vr8
    vsra.w          vr18,   vr18,  vr7
    vsra.w          vr19,   vr19,  vr7
    vsra.w          vr20,   vr20,  vr7
    vsra.w          vr21,   vr21,  vr7
    vpickev.h       vr18,   vr19,  vr18
    vpickev.h       vr20,   vr21,  vr20
    vst             vr18,   a0,    0
    vst             vr20,   a1,    0
    addi.d          a3,     a3,    -8
    addi.d          a5,     a5,    8
    addi.d          a6,     a6,    8
    addi.d          a7,     a7,    8
    addi.d          a0,     a0,    16
    addi.d          a1,     a1,    16
    bge             a3,     t8,    .LOOP_WIDTH8
    bge             a3,     t7,    .LOOP_WIDTH4
    blt             zero,   a3,    .LOOP_WIDTH
    b               .LOOP_END

.LOOP_WIDTH4:
    vld             vr9,    a5,    0
    vld             vr10,   a6,    0
    vld             vr11,   a7,    0
    vilvl.b         vr9,    vr0,   vr9
    vilvl.b         vr10,   vr0,   vr10
    vilvl.b         vr11,   vr0,   vr11
    vilvl.h         vr12,   vr0,   vr9
    vilvl.h         vr13,   vr0,   vr10
    vilvl.h         vr14,   vr0,   vr11
    vmul.w          vr18,   vr1,   vr14
    vmul.w          vr19,   vr4,   vr14
    vmadd.w         vr18,   vr2,   vr12
    vmadd.w         vr18,   vr3,   vr13
    vmadd.w         vr19,   vr5,   vr12
    vmadd.w         vr19,   vr6,   vr13
    vadd.w          vr18,   vr18,  vr8
    vadd.w          vr19,   vr19,  vr8
    vsra.w          vr18,   vr18,  vr7
    vsra.w          vr19,   vr19,  vr7
    vpickev.h       vr18,   vr18,  vr18
    vpickev.h       vr19,   vr19,  vr19
    vstelm.d        vr18,   a0,    0,    0
    vstelm.d        vr19,   a1,    0,    0
    addi.d          a3,     a3,    -4
    addi.d          a5,     a5,    4
    addi.d          a6,     a6,    4
    addi.d          a7,     a7,    4
    addi.d          a0,     a0,    8
    addi.d          a1,     a1,    8
    bge             a3,     t7,    .LOOP_WIDTH4
    blt             zero,   a3,    .LOOP_WIDTH
    b               .LOOP_END

.LOOP_WIDTH:
    ld.bu           t0,     a5,    0
    ld.bu           t4,     a6,    0
    ld.bu           t6,     a7,    0
    mul.w           t8,     t6,    t1
    mul.w           t7,     t0,    t2
    add.w           t8,     t8,    t7
    mul.w           t7,     t4,    t3
    add.w           t8,     t8,    t7
    add.w           t8,     t8,    t5
    srai.w          t8,     t8,    9
    st.h            t8,     a0,    0
    mul.w           t8,     t6,    s1
    mul.w           t7,     t0,    s2
    add.w           t8,     t8,    t7
    mul.w           t7,     t4,    s3
    add.w           t8,     t8,    t7
    add.w           t8,     t8,    t5
    srai.w          t8,     t8,    9
    st.h            t8,     a1,    0
    addi.d          a3,     a3,    -1
    addi.d          a5,     a5,    1
    addi.d          a6,     a6,    1
    addi.d          a7,     a7,    1
    addi.d          a0,     a0,    2
    addi.d          a1,     a1,    2
    blt             zero,   a3,    .LOOP_WIDTH

.LOOP_END:
    ld.d            s1,     sp,    0
    ld.d            s2,     sp,    8
    ld.d            s3,     sp,    16
    addi.d          sp,     sp,    24
endfunc

/*
 * void yuy2ToUV_lsx(uint8_t *dstU, uint8_t *dstV, const uint8_t *unused0, const uint8_t *src1,
 *                   const uint8_t *src2, int width, uint32_t *unused, void *opq)
 */
function yuy2ToUV_lsx
    andi         t0,    a5,    7
    srli.d       a5,    a5,    3
    beqz         a5,    2f
1:
    vld          vr0,   a3,    1
    vld          vr1,   a3,    17
    addi.d       a5,    a5,    -1
    addi.d       a3,    a3,    32
    vpickev.b    vr2,   vr1,   vr0
    vpickev.b    vr0,   vr2,   vr2
    vpickod.b    vr1,   vr2,   vr2
    fst.d        f0,    a0,    0
    fst.d        f1,    a1,    0
    addi.d       a0,    a0,    8
    addi.d       a1,    a1,    8
    bnez         a5,    1b
2:
    beqz         t0,    4f
3:
    ld.b         t1,    a3,    1
    ld.b         t2,    a3,    3
    addi.d       a3,    a3,    4
    addi.d       t0,    t0,    -1
    st.b         t1,    a0,    0
    st.b         t2,    a1,    0
    addi.d       a0,    a0,    1
    addi.d       a1,    a1,    1
    bnez         t0,    3b
4:
endfunc

function yuy2ToUV_lasx
    andi         t0,    a5,    15
    srli.d       a5,    a5,    4
    beqz         a5,    2f
1:
    xvld         xr0,   a3,    1
    xvld         xr1,   a3,    33
    addi.d       a5,    a5,    -1
    addi.d       a3,    a3,    64
    xvpickev.b   xr2,   xr1,   xr0
    xvpermi.d    xr2,   xr2,   0xd8
    xvpickev.b   xr0,   xr2,   xr2
    xvpermi.d    xr0,   xr0,   0xd8
    xvpickod.b   xr1,   xr2,   xr2
    xvpermi.d    xr1,   xr1,   0xd8
    vst          vr0,   a0,    0
    vst          vr1,   a1,    0
    addi.d       a0,    a0,    16
    addi.d       a1,    a1,    16
    bnez         a5,    1b
2:
    beqz         t0,    4f
3:
    ld.b         t1,    a3,    1
    ld.b         t2,    a3,    3
    addi.d       a3,    a3,    4
    addi.d       t0,    t0,    -1
    st.b         t1,    a0,    0
    st.b         t2,    a1,    0
    addi.d       a0,    a0,    1
    addi.d       a1,    a1,    1
    bnez         t0,    3b
4:
endfunc

/*
 * void yvy2ToUV_lsx(uint8_t *dstU, uint8_t *dstV, const uint8_t *unused0, const uint8_t *src1,
 *                   const uint8_t *src2, int width, uint32_t *unused, void *opq)
 */
function yvy2ToUV_lsx
    andi         t0,    a5,    7
    srli.d       a5,    a5,    3
    beqz         a5,    2f
1:
    vld          vr0,   a3,    1
    vld          vr1,   a3,    17
    addi.d       a5,    a5,    -1
    addi.d       a3,    a3,    32
    vpickev.b    vr2,   vr1,   vr0
    vpickev.b    vr0,   vr2,   vr2
    vpickod.b    vr1,   vr2,   vr2
    fst.d        f0,    a1,    0
    fst.d        f1,    a0,    0
    addi.d       a0,    a0,    8
    addi.d       a1,    a1,    8
    bnez         a5,    1b
2:
    beqz         t0,    4f
3:
    ld.b         t1,    a3,    1
    ld.b         t2,    a3,    3
    addi.d       a3,    a3,    4
    addi.d       t0,    t0,    -1
    st.b         t1,    a1,    0
    st.b         t2,    a0,    0
    addi.d       a0,    a0,    1
    addi.d       a1,    a1,    1
    bnez         t0,    3b
4:
endfunc

function yvy2ToUV_lasx
    andi         t0,    a5,    15
    srli.d       a5,    a5,    4
    beqz         a5,    2f
1:
    xvld         xr0,   a3,    1
    xvld         xr1,   a3,    33
    addi.d       a5,    a5,    -1
    addi.d       a3,    a3,    64
    xvpickev.b   xr2,   xr1,   xr0
    xvpermi.d    xr2,   xr2,   0xd8
    xvpickev.b   xr0,   xr2,   xr2
    xvpermi.d    xr0,   xr0,   0xd8
    xvpickod.b   xr1,   xr2,   xr2
    xvpermi.d    xr1,   xr1,   0xd8
    vst          vr0,   a1,    0
    vst          vr1,   a0,    0
    addi.d       a0,    a0,    16
    addi.d       a1,    a1,    16
    bnez         a5,    1b
2:
    beqz         t0,    4f
3:
    ld.b         t1,    a3,    1
    ld.b         t2,    a3,    3
    addi.d       a3,    a3,    4
    addi.d       t0,    t0,    -1
    st.b         t1,    a1,    0
    st.b         t2,    a0,    0
    addi.d       a0,    a0,    1
    addi.d       a1,    a1,    1
    bnez         t0,    3b
4:
endfunc

/*
 * void uyvyToUV_lsx(uint8_t *dstU, uint8_t *dstV, const uint8_t *unused0, const uint8_t *src1,
 *                   const uint8_t *src2, int width, uint32_t *unused, void *opq)
 */
function uyvyToUV_lsx
    andi         t0,    a5,    7
    srli.d       a5,    a5,    3
    beqz         a5,    2f
1:
    vld          vr0,   a3,    0
    vld          vr1,   a3,    16
    addi.d       a5,    a5,    -1
    addi.d       a3,    a3,    32
    vpickev.b    vr2,   vr1,   vr0
    vpickev.b    vr0,   vr2,   vr2
    vpickod.b    vr1,   vr2,   vr2
    fst.d        f0,    a0,    0
    fst.d        f1,    a1,    0
    addi.d       a0,    a0,    8
    addi.d       a1,    a1,    8
    bnez         a5,    1b
2:
    beqz         t0,    4f
3:
    ld.b         t1,    a3,    1
    ld.b         t2,    a3,    3
    addi.d       a3,    a3,    4
    addi.d       t0,    t0,    -1
    st.b         t1,    a0,    0
    st.b         t2,    a1,    0
    addi.d       a0,    a0,    1
    addi.d       a1,    a1,    1
    bnez         t0,    3b
4:
endfunc

function uyvyToUV_lasx
    andi         t0,    a5,    15
    srli.d       a5,    a5,    4
    beqz         a5,    2f
1:
    xvld         xr0,   a3,    0
    xvld         xr1,   a3,    32
    addi.d       a5,    a5,    -1
    addi.d       a3,    a3,    64
    xvpickev.b   xr2,   xr1,   xr0
    xvpermi.d    xr2,   xr2,   0xd8
    xvpickev.b   xr0,   xr2,   xr2
    xvpermi.d    xr0,   xr0,   0xd8
    xvpickod.b   xr1,   xr2,   xr2
    xvpermi.d    xr1,   xr1,   0xd8
    vst          vr0,   a0,    0
    vst          vr1,   a1,    0
    addi.d       a0,    a0,    16
    addi.d       a1,    a1,    16
    bnez         a5,    1b
2:
    beqz         t0,    4f
3:
    ld.b         t1,    a3,    1
    ld.b         t2,    a3,    3
    addi.d       a3,    a3,    4
    addi.d       t0,    t0,    -1
    st.b         t1,    a0,    0
    st.b         t2,    a1,    0
    addi.d       a0,    a0,    1
    addi.d       a1,    a1,    1
    bnez         t0,    3b
4:
endfunc

/*
 * void nv12ToUV_lsx(uint8_t *dstU, uint8_t *dstV, const uint8_t *unused0, const uint8_t *src1,
 *                   const uint8_t *src2, int width, uint32_t *unused, void *opq)
 */
function nv12ToUV_lsx
    andi         t0,    a5,    15
    srli.d       a5,    a5,    4
    beqz         a5,    2f
1:
    vld          vr0,   a3,    0
    vld          vr1,   a3,    16
    addi.d       a5,    a5,    -1
    addi.d       a3,    a3,    32
    vpickev.b    vr2,   vr1,   vr0
    vpickod.b    vr3,   vr1,   vr0
    vst          vr2,   a0,    0
    vst          vr3,   a1,    0
    addi.d       a0,    a0,    16
    addi.d       a1,    a1,    16
    bnez         a5,    1b
2:
    beqz         t0,    4f
3:
    ld.b         t1,    a3,    0
    ld.b         t2,    a3,    1
    addi.d       a3,    a3,    2
    addi.d       t0,    t0,    -1
    st.b         t1,    a0,    0
    st.b         t2,    a1,    0
    addi.d       a0,    a0,    1
    addi.d       a1,    a1,    1
    bnez         t0,    3b
4:
endfunc

function nv12ToUV_lasx
    andi         t0,    a5,    31
    srli.d       a5,    a5,    5
    beqz         a5,    2f
1:
    xvld         xr0,   a3,    0
    xvld         xr1,   a3,    32
    addi.d       a5,    a5,    -1
    addi.d       a3,    a3,    64
    xvpickev.b   xr2,   xr1,   xr0
    xvpickod.b   xr3,   xr1,   xr0
    xvpermi.d    xr2,   xr2,   0xd8
    xvpermi.d    xr3,   xr3,   0xd8
    xvst         xr2,   a0,    0
    xvst         xr3,   a1,    0
    addi.d       a0,    a0,    32
    addi.d       a1,    a1,    32
    bnez         a5,    1b
2:
    beqz         t0,    4f
3:
    ld.b         t1,    a3,    0
    ld.b         t2,    a3,    1
    addi.d       a3,    a3,    2
    addi.d       t0,    t0,    -1
    st.b         t1,    a0,    0
    st.b         t2,    a1,    0
    addi.d       a0,    a0,    1
    addi.d       a1,    a1,    1
    bnez         t0,    3b
4:
endfunc

/*
 * void nv21ToUV_lsx(uint8_t *dstU, uint8_t *dstV, const uint8_t *unused0, const uint8_t *src1,
 *                   const uint8_t *src2, int width, uint32_t *unused, void *opq)
 */
function nv21ToUV_lsx
    andi         t0,    a5,    15
    srli.d       a5,    a5,    4
    beqz         a5,    2f
1:
    vld          vr0,   a3,    0
    vld          vr1,   a3,    16
    addi.d       a5,    a5,    -1
    addi.d       a3,    a3,    32
    vpickev.b    vr2,   vr1,   vr0
    vpickod.b    vr3,   vr1,   vr0
    vst          vr2,   a1,    0
    vst          vr3,   a0,    0
    addi.d       a0,    a0,    16
    addi.d       a1,    a1,    16
    bnez         a5,    1b
2:
    beqz         t0,    4f
3:
    ld.b         t1,    a3,    0
    ld.b         t2,    a3,    1
    addi.d       a3,    a3,    2
    addi.d       t0,    t0,    -1
    st.b         t1,    a1,    0
    st.b         t2,    a0,    0
    addi.d       a0,    a0,    1
    addi.d       a1,    a1,    1
    bnez         t0,    3b
4:
endfunc

function nv21ToUV_lasx
    andi         t0,    a5,    31
    srli.d       a5,    a5,    5
    beqz         a5,    2f
1:
    xvld         xr0,   a3,    0
    xvld         xr1,   a3,    32
    addi.d       a5,    a5,    -1
    addi.d       a3,    a3,    64
    xvpickev.b   xr2,   xr1,   xr0
    xvpickod.b   xr3,   xr1,   xr0
    xvpermi.d    xr2,   xr2,   0xd8
    xvpermi.d    xr3,   xr3,   0xd8
    xvst         xr2,   a1,    0
    xvst         xr3,   a0,    0
    addi.d       a0,    a0,    32
    addi.d       a1,    a1,    32
    bnez         a5,    1b
2:
    beqz         t0,    4f
3:
    ld.b         t1,    a3,    0
    ld.b         t2,    a3,    1
    addi.d       a3,    a3,    2
    addi.d       t0,    t0,    -1
    st.b         t1,    a1,    0
    st.b         t2,    a0,    0
    addi.d       a0,    a0,    1
    addi.d       a1,    a1,    1
    bnez         t0,    3b
4:
endfunc

/*
 *void abgrToA_lsx(uint8_t *_dst, const uint8_t *src, const uint8_t *unused1,
 *                 const uint8_t *unused2, int width, uint32_t *unused, void *opq)
 */
function abgrToA_lsx
    andi         t0,    a4,    7
    srli.d       a4,    a4,    3
    vxor.v       vr0,   vr0,   vr0
    beqz         a4,    2f
1:
    vld          vr1,   a1,    0
    vld          vr2,   a1,    16
    addi.d       a4,    a4,    -1
    addi.d       a1,    a1,    32
    vpickev.b    vr3,   vr2,   vr1
    vpackev.b    vr3,   vr0,   vr3
    vslli.h      vr1,   vr3,   6
    vsrli.h      vr2,   vr3,   2
    vor.v        vr3,   vr2,   vr1
    vst          vr3,   a0,    0
    addi.d       a0,    a0,    16
    bnez         a4,    1b
2:
    beqz         t0,    4f
3:
    ld.b         t1,    a1,    3
    addi.d       t0,    t0,    -1
    addi.d       a1,    a1,    4
    andi         t1,    t1,    0xff
    slli.w       t2,    t1,    6
    srli.w       t3,    t1,    2
    or           t1,    t2,    t3
    st.h         t1,    a0,    0
    addi.d       a0,    a0,    2
    bnez         t0,    3b
4:
endfunc

function abgrToA_lasx
    andi         t0,    a4,    15
    srli.d       a4,    a4,    4
    xvxor.v      xr0,   xr0,   xr0
    beqz         a4,    2f
1:
    xvld         xr1,   a1,    0
    xvld         xr2,   a1,    32
    addi.d       a4,    a4,    -1
    addi.d       a1,    a1,    64
    xvpickev.b   xr3,   xr2,   xr1
    xvpermi.d    xr3,   xr3,   0xd8
    xvpackev.b   xr3,   xr0,   xr3
    xvslli.h     xr1,   xr3,   6
    xvsrli.h     xr2,   xr3,   2
    xvor.v       xr3,   xr2,   xr1
    xvst         xr3,   a0,    0
    addi.d       a0,    a0,    32
    bnez         a4,    1b
2:
    beqz         t0,    4f
3:
    ld.b         t1,    a1,    3
    addi.d       t0,    t0,    -1
    addi.d       a1,    a1,    4
    andi         t1,    t1,    0xff
    slli.w       t2,    t1,    6
    srli.w       t3,    t1,    2
    or           t1,    t2,    t3
    st.h         t1,    a0,    0
    addi.d       a0,    a0,    2
    bnez         t0,    3b
4:
endfunc

/*
 *void rgbaToA_lsx(uint8_t *_dst, const uint8_t *src, const uint8_t *unused1,
 *                 const uint8_t *unused2, int width, uint32_t *unused, void *opq)
 */
function rgbaToA_lsx
    andi         t0,    a4,    7
    srli.d       a4,    a4,    3
    vxor.v       vr0,   vr0,   vr0
    beqz         a4,    2f
1:
    vld          vr1,   a1,    3
    vld          vr2,   a1,    19
    addi.d       a4,    a4,    -1
    addi.d       a1,    a1,    32
    vpickev.b    vr3,   vr2,   vr1
    vpackev.b    vr3,   vr0,   vr3
    vslli.h      vr1,   vr3,   6
    vsrli.h      vr2,   vr3,   2
    vor.v        vr3,   vr2,   vr1
    vst          vr3,   a0,    0
    addi.d       a0,    a0,    16
    bnez         a4,    1b
2:
    beqz         t0,    4f
3:
    ld.b         t1,    a1,    3
    addi.d       t0,    t0,    -1
    addi.d       a1,    a1,    4
    andi         t1,    t1,    0xff
    slli.w       t2,    t1,    6
    srli.w       t3,    t1,    2
    or           t1,    t2,    t3
    st.h         t1,    a0,    0
    addi.d       a0,    a0,    2
    bnez         t0,    3b
4:
endfunc

function rgbaToA_lasx
    andi         t0,    a4,    15
    srli.d       a4,    a4,    4
    xvxor.v      xr0,   xr0,   xr0
    beqz         a4,    2f
1:
    xvld         xr1,   a1,    3
    xvld         xr2,   a1,    35
    addi.d       a4,    a4,    -1
    addi.d       a1,    a1,    64
    xvpickev.b   xr3,   xr2,   xr1
    xvpermi.d    xr3,   xr3,   0xd8
    xvpackev.b   xr3,   xr0,   xr3
    xvslli.h     xr1,   xr3,   6
    xvsrli.h     xr2,   xr3,   2
    xvor.v       xr3,   xr2,   xr1
    xvst         xr3,   a0,    0
    addi.d       a0,    a0,    32
    bnez         a4,    1b
2:
    beqz         t0,    4f
3:
    ld.b         t1,    a1,    3
    addi.d       t0,    t0,    -1
    addi.d       a1,    a1,    4
    andi         t1,    t1,    0xff
    slli.w       t2,    t1,    6
    srli.w       t3,    t1,    2
    or           t1,    t2,    t3
    st.h         t1,    a0,    0
    addi.d       a0,    a0,    2
    bnez         t0,    3b
4:
endfunc

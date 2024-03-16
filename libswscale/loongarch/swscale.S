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

/* void ff_hscale_8_to_15_lsx(SwsContext *c, int16_t *dst, int dstW,
 *                            const uint8_t *src, const int16_t *filter,
 *                            const int32_t *filterPos, int filterSize)
 */
function ff_hscale_8_to_15_lsx
    addi.d           sp,      sp,     -72
    st.d             s0,      sp,     0
    st.d             s1,      sp,     8
    st.d             s2,      sp,     16
    st.d             s3,      sp,     24
    st.d             s4,      sp,     32
    st.d             s5,      sp,     40
    st.d             s6,      sp,     48
    st.d             s7,      sp,     56
    st.d             s8,      sp,     64
    li.w             t0,      32767
    li.w             t8,      8
    li.w             t7,      4
    vldi             vr0,     0
    vreplgr2vr.w     vr20,    t0
    beq              a6,      t7,     .LOOP_DSTW4
    beq              a6,      t8,     .LOOP_DSTW8
    blt              t8,      a6,     .LOOP_START
    b                .END_DSTW4

.LOOP_START:
    li.w             t1,      0
    li.w             s1,      0
    li.w             s2,      0
    li.w             s3,      0
    li.w             s4,      0
    li.w             s5,      0
    vldi             vr22,    0
    addi.w           s0,      a6,     -7
    slli.w           s7,      a6,     1
    slli.w           s8,      a6,     2
    add.w            t6,      s7,     s8
.LOOP_DSTW:
    ld.w             t2,      a5,     0
    ld.w             t3,      a5,     4
    ld.w             t4,      a5,     8
    ld.w             t5,      a5,     12
    fldx.d           f1,      a3,     t2
    fldx.d           f2,      a3,     t3
    fldx.d           f3,      a3,     t4
    fldx.d           f4,      a3,     t5
    vld              vr9,     a4,     0
    vldx             vr10,    a4,     s7
    vldx             vr11,    a4,     s8
    vldx             vr12,    a4,     t6
    vilvl.b          vr1,     vr0,    vr1
    vilvl.b          vr2,     vr0,    vr2
    vilvl.b          vr3,     vr0,    vr3
    vilvl.b          vr4,     vr0,    vr4
    vdp2.w.h         vr17,    vr1,    vr9
    vdp2.w.h         vr18,    vr2,    vr10
    vdp2.w.h         vr19,    vr3,    vr11
    vdp2.w.h         vr21,    vr4,    vr12
    vhaddw.d.w       vr1,     vr17,   vr17
    vhaddw.d.w       vr2,     vr18,   vr18
    vhaddw.d.w       vr3,     vr19,   vr19
    vhaddw.d.w       vr4,     vr21,   vr21
    vhaddw.q.d       vr1,     vr1,    vr1
    vhaddw.q.d       vr2,     vr2,    vr2
    vhaddw.q.d       vr3,     vr3,    vr3
    vhaddw.q.d       vr4,     vr4,    vr4
    vilvl.w          vr1,     vr2,    vr1
    vilvl.w          vr3,     vr4,    vr3
    vilvl.d          vr1,     vr3,    vr1
    vadd.w           vr22,    vr22,   vr1
    addi.w           s1,      s1,     8
    addi.d           a3,      a3,     8
    addi.d           a4,      a4,     16
    blt              s1,      s0,     .LOOP_DSTW
    blt              s1,      a6,     .DSTWA
    b                .END_FILTER
.DSTWA:
    ld.w             t2,      a5,     0
    li.w             t3,      0
    move             s6,      s1
.FILTERSIZEA:
    add.w            t4,      t2,     t3
    ldx.bu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t6,      t6,     1
    ldx.h            t6,      a4,     t6
    mul.w            t6,      t5,     t6
    add.w            s2,      s2,     t6
    addi.w           t3,      t3,     1
    addi.w           s6,      s6,     1
    blt              s6,      a6,     .FILTERSIZEA

    ld.w             t2,      a5,     4
    li.w             t3,      0
    move             s6,      s1
    addi.w           t1,      t1,     1
.FILTERSIZEB:
    add.w            t4,      t2,     t3
    ldx.bu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t6,      t6,     1
    ldx.h            t6,      a4,     t6
    mul.w            t6,      t5,     t6
    add.w            s3,      s3,     t6
    addi.w           t3,      t3,     1
    addi.w           s6,      s6,     1
    blt              s6,      a6,     .FILTERSIZEB
    ld.w             t2,      a5,     8
    addi.w           t1,      t1,     1
    li.w             t3,      0
    move             s6,      s1
.FILTERSIZEC:
    add.w            t4,      t2,     t3
    ldx.bu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t6,      t6,     1
    ldx.h            t6,      a4,     t6
    mul.w            t6,      t5,     t6
    add.w            s4,      s4,     t6
    addi.w           t3,      t3,     1
    addi.w           s6,      s6,     1
    blt              s6,      a6,     .FILTERSIZEC
    ld.w             t2,      a5,     12
    addi.w           t1,      t1,     1
    move             s6,      s1
    li.w             t3,      0
.FILTERSIZED:
    add.w            t4,      t2,     t3
    ldx.bu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t6,      t6,     1
    ldx.h            t6,      a4,     t6
    mul.w            t6,      t5,     t6
    add.w            s5,      s5,     t6
    addi.w           t3,      t3,     1
    addi.w           s6,      s6,     1
    blt              s6,      a6,     .FILTERSIZED
.END_FILTER:
    vpickve2gr.w     t1,      vr22,   0
    vpickve2gr.w     t2,      vr22,   1
    vpickve2gr.w     t3,      vr22,   2
    vpickve2gr.w     t4,      vr22,   3
    add.w            s2,      s2,     t1
    add.w            s3,      s3,     t2
    add.w            s4,      s4,     t3
    add.w            s5,      s5,     t4
    srai.w           s2,      s2,     7
    srai.w           s3,      s3,     7
    srai.w           s4,      s4,     7
    srai.w           s5,      s5,     7
    slt              t1,      s2,     t0
    slt              t2,      s3,     t0
    slt              t3,      s4,     t0
    slt              t4,      s5,     t0
    maskeqz          s2,      s2,     t1
    maskeqz          s3,      s3,     t2
    maskeqz          s4,      s4,     t3
    maskeqz          s5,      s5,     t4
    masknez          t1,      t0,     t1
    masknez          t2,      t0,     t2
    masknez          t3,      t0,     t3
    masknez          t4,      t0,     t4
    or               s2,      s2,     t1
    or               s3,      s3,     t2
    or               s4,      s4,     t3
    or               s5,      s5,     t4
    st.h             s2,      a1,     0
    st.h             s3,      a1,     2
    st.h             s4,      a1,     4
    st.h             s5,      a1,     6

    addi.d           a1,      a1,     8
    sub.d            a3,      a3,     s1
    addi.d           a5,      a5,     16
    slli.d           t3,      a6,     3
    add.d            a4,      a4,     t3
    sub.d            a4,      a4,     s1
    sub.d            a4,      a4,     s1
    addi.d           a2,      a2,     -4
    bge              a2,      t7,     .LOOP_START
    blt              zero,    a2,     .RES
    b                .END_LOOP
.RES:
    li.w             t1,      0
.DSTW:
    slli.w           t2,      t1,     2
    ldx.w            t2,      a5,     t2
    li.w             t3,      0
    li.w             t8,      0
.FILTERSIZE:
    add.w            t4,      t2,     t3
    ldx.bu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t7,      t6,     1
    ldx.h            t7,      a4,     t7
    mul.w            t7,      t5,     t7
    add.w            t8,      t8,     t7
    addi.w           t3,      t3,     1
    blt              t3,      a6,     .FILTERSIZE
    srai.w           t8,      t8,     7
    slt              t5,      t8,     t0
    maskeqz          t8,      t8,     t5
    masknez          t5,      t0,     t5
    or               t8,      t8,     t5
    slli.w           t4,      t1,     1
    stx.h            t8,      a1,     t4
    addi.w           t1,      t1,     1
    blt              t1,      a2,     .DSTW
    b                .END_LOOP

.LOOP_DSTW8:
    ld.w             t1,      a5,     0
    ld.w             t2,      a5,     4
    ld.w             t3,      a5,     8
    ld.w             t4,      a5,     12
    fldx.d           f1,      a3,     t1
    fldx.d           f2,      a3,     t2
    fldx.d           f3,      a3,     t3
    fldx.d           f4,      a3,     t4
    ld.w             t1,      a5,     16
    ld.w             t2,      a5,     20
    ld.w             t3,      a5,     24
    ld.w             t4,      a5,     28
    fldx.d           f5,      a3,     t1
    fldx.d           f6,      a3,     t2
    fldx.d           f7,      a3,     t3
    fldx.d           f8,      a3,     t4
    vld              vr9,     a4,     0
    vld              vr10,    a4,     16
    vld              vr11,    a4,     32
    vld              vr12,    a4,     48
    vld              vr13,    a4,     64
    vld              vr14,    a4,     80
    vld              vr15,    a4,     96
    vld              vr16,    a4,     112
    vilvl.b          vr1,     vr0,    vr1
    vilvl.b          vr2,     vr0,    vr2
    vilvl.b          vr3,     vr0,    vr3
    vilvl.b          vr4,     vr0,    vr4
    vilvl.b          vr5,     vr0,    vr5
    vilvl.b          vr6,     vr0,    vr6
    vilvl.b          vr7,     vr0,    vr7
    vilvl.b          vr8,     vr0,    vr8

    vdp2.w.h         vr17,    vr1,    vr9
    vdp2.w.h         vr18,    vr2,    vr10
    vdp2.w.h         vr19,    vr3,    vr11
    vdp2.w.h         vr21,    vr4,    vr12
    vdp2.w.h         vr1,     vr5,    vr13
    vdp2.w.h         vr2,     vr6,    vr14
    vdp2.w.h         vr3,     vr7,    vr15
    vdp2.w.h         vr4,     vr8,    vr16
    vhaddw.d.w       vr5,     vr1,    vr1
    vhaddw.d.w       vr6,     vr2,    vr2
    vhaddw.d.w       vr7,     vr3,    vr3
    vhaddw.d.w       vr8,     vr4,    vr4
    vhaddw.d.w       vr1,     vr17,   vr17
    vhaddw.d.w       vr2,     vr18,   vr18
    vhaddw.d.w       vr3,     vr19,   vr19
    vhaddw.d.w       vr4,     vr21,   vr21
    vhaddw.q.d       vr1,     vr1,    vr1
    vhaddw.q.d       vr2,     vr2,    vr2
    vhaddw.q.d       vr3,     vr3,    vr3
    vhaddw.q.d       vr4,     vr4,    vr4
    vhaddw.q.d       vr5,     vr5,    vr5
    vhaddw.q.d       vr6,     vr6,    vr6
    vhaddw.q.d       vr7,     vr7,    vr7
    vhaddw.q.d       vr8,     vr8,    vr8
    vilvl.w          vr1,     vr2,    vr1
    vilvl.w          vr3,     vr4,    vr3
    vilvl.w          vr5,     vr6,    vr5
    vilvl.w          vr7,     vr8,    vr7
    vilvl.d          vr1,     vr3,    vr1
    vilvl.d          vr5,     vr7,    vr5
    vsrai.w          vr1,     vr1,    7
    vsrai.w          vr5,     vr5,    7
    vmin.w           vr1,     vr1,    vr20
    vmin.w           vr5,     vr5,    vr20

    vpickev.h        vr1,     vr5,    vr1
    vst              vr1,     a1,     0
    addi.d           a1,      a1,     16
    addi.d           a5,      a5,     32
    addi.d           a4,      a4,     128
    addi.d           a2,      a2,     -8
    bge              a2,      t8,     .LOOP_DSTW8
    blt              zero,    a2,     .RES8
    b                .END_LOOP
.RES8:
    li.w             t1,      0
.DSTW8:
    slli.w           t2,      t1,     2
    ldx.w            t2,      a5,     t2
    li.w             t3,      0
    li.w             t8,      0
.FILTERSIZE8:
    add.w            t4,      t2,     t3
    ldx.bu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t7,      t6,     1
    ldx.h            t7,      a4,     t7
    mul.w            t7,      t5,     t7
    add.w            t8,      t8,     t7
    addi.w           t3,      t3,     1
    blt              t3,      a6,     .FILTERSIZE8
    srai.w           t8,      t8,     7
    slt              t5,      t8,     t0
    maskeqz          t8,      t8,     t5
    masknez          t5,      t0,     t5
    or               t8,      t8,     t5
    slli.w           t4,      t1,     1
    stx.h            t8,      a1,     t4
    addi.w           t1,      t1,     1
    blt              t1,      a2,     .DSTW8
    b                .END_LOOP

.LOOP_DSTW4:
    ld.w             t1,      a5,     0
    ld.w             t2,      a5,     4
    ld.w             t3,      a5,     8
    ld.w             t4,      a5,     12
    fldx.s           f1,      a3,     t1
    fldx.s           f2,      a3,     t2
    fldx.s           f3,      a3,     t3
    fldx.s           f4,      a3,     t4
    ld.w             t1,      a5,     16
    ld.w             t2,      a5,     20
    ld.w             t3,      a5,     24
    ld.w             t4,      a5,     28
    fldx.s           f5,      a3,     t1
    fldx.s           f6,      a3,     t2
    fldx.s           f7,      a3,     t3
    fldx.s           f8,      a3,     t4
    vld              vr9,     a4,     0
    vld              vr10,    a4,     16
    vld              vr11,    a4,     32
    vld              vr12,    a4,     48
    vilvl.w          vr1,     vr2,    vr1
    vilvl.w          vr3,     vr4,    vr3
    vilvl.w          vr5,     vr6,    vr5
    vilvl.w          vr7,     vr8,    vr7
    vilvl.b          vr1,     vr0,    vr1
    vilvl.b          vr3,     vr0,    vr3
    vilvl.b          vr5,     vr0,    vr5
    vilvl.b          vr7,     vr0,    vr7

    vdp2.w.h         vr13,    vr1,    vr9
    vdp2.w.h         vr14,    vr3,    vr10
    vdp2.w.h         vr15,    vr5,    vr11
    vdp2.w.h         vr16,    vr7,    vr12
    vhaddw.d.w       vr13,    vr13,   vr13
    vhaddw.d.w       vr14,    vr14,   vr14
    vhaddw.d.w       vr15,    vr15,   vr15
    vhaddw.d.w       vr16,    vr16,   vr16
    vpickev.w        vr13,    vr14,   vr13
    vpickev.w        vr15,    vr16,   vr15
    vsrai.w          vr13,    vr13,   7
    vsrai.w          vr15,    vr15,   7
    vmin.w           vr13,    vr13,   vr20
    vmin.w           vr15,    vr15,   vr20

    vpickev.h        vr13,    vr15,   vr13
    vst              vr13,    a1,     0
    addi.d           a1,      a1,     16
    addi.d           a5,      a5,     32
    addi.d           a4,      a4,     64
    addi.d           a2,      a2,     -8
    bge              a2,      t8,     .LOOP_DSTW4
    blt              zero,    a2,     .RES4
    b                .END_LOOP
.RES4:
    li.w             t1,      0
.DSTW4:
    slli.w           t2,      t1,     2
    ldx.w            t2,      a5,     t2
    li.w             t3,      0
    li.w             t8,      0
.FILTERSIZE4:
    add.w            t4,      t2,     t3
    ldx.bu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t7,      t6,     1
    ldx.h            t7,      a4,     t7
    mul.w            t7,      t5,     t7
    add.w            t8,      t8,     t7
    addi.w           t3,      t3,     1
    blt              t3,      a6,     .FILTERSIZE4
    srai.w           t8,      t8,     7
    slt              t5,      t8,     t0
    maskeqz          t8,      t8,     t5
    masknez          t5,      t0,     t5
    or               t8,      t8,     t5
    slli.w           t4,      t1,     1
    stx.h            t8,      a1,     t4
    addi.w           t1,      t1,     1
    blt              t1,      a2,     .DSTW4
    b                .END_LOOP
.END_DSTW4:

    li.w             t1,      0
.LOOP_DSTW1:
    slli.w           t2,      t1,     2
    ldx.w            t2,      a5,     t2
    li.w             t3,      0
    li.w             t8,      0
.FILTERSIZE1:
    add.w            t4,      t2,     t3
    ldx.bu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t7,      t6,     1
    ldx.h            t7,      a4,     t7
    mul.w            t7,      t5,     t7
    add.w            t8,      t8,     t7
    addi.w           t3,      t3,     1
    blt              t3,      a6,     .FILTERSIZE1
    srai.w           t8,      t8,     7
    slt              t5,      t8,     t0
    maskeqz          t8,      t8,     t5
    masknez          t5,      t0,     t5
    or               t8,      t8,     t5
    slli.w           t4,      t1,     1
    stx.h            t8,      a1,     t4
    addi.w           t1,      t1,     1
    blt              t1,      a2,     .LOOP_DSTW1
    b                .END_LOOP
.END_LOOP:

    ld.d             s0,      sp,     0
    ld.d             s1,      sp,     8
    ld.d             s2,      sp,     16
    ld.d             s3,      sp,     24
    ld.d             s4,      sp,     32
    ld.d             s5,      sp,     40
    ld.d             s6,      sp,     48
    ld.d             s7,      sp,     56
    ld.d             s8,      sp,     64
    addi.d           sp,      sp,     72
endfunc

/* void ff_hscale_8_to_19_lsx(SwsContext *c, int16_t *dst, int dstW,
 *                            const uint8_t *src, const int16_t *filter,
 *                            const int32_t *filterPos, int filterSize)
 */
function ff_hscale_8_to_19_lsx
    addi.d           sp,      sp,     -72
    st.d             s0,      sp,     0
    st.d             s1,      sp,     8
    st.d             s2,      sp,     16
    st.d             s3,      sp,     24
    st.d             s4,      sp,     32
    st.d             s5,      sp,     40
    st.d             s6,      sp,     48
    st.d             s7,      sp,     56
    st.d             s8,      sp,     64
    li.w             t0,      524287
    li.w             t8,      8
    li.w             t7,      4
    vldi             vr0,     0
    vreplgr2vr.w     vr20,    t0
    beq              a6,      t7,     .LOOP_DST4
    beq              a6,      t8,     .LOOP_DST8
    blt              t8,      a6,     .LOOP
    b                .END_DST4

.LOOP:
    li.w             t1,      0
    li.w             s1,      0
    li.w             s2,      0
    li.w             s3,      0
    li.w             s4,      0
    li.w             s5,      0
    vldi             vr22,    0
    addi.w           s0,      a6,     -7
    slli.w           s7,      a6,     1
    slli.w           s8,      a6,     2
    add.w            t6,      s7,     s8
.LOOP_DST:
    ld.w             t2,      a5,     0
    ld.w             t3,      a5,     4
    ld.w             t4,      a5,     8
    ld.w             t5,      a5,     12
    fldx.d           f1,      a3,     t2
    fldx.d           f2,      a3,     t3
    fldx.d           f3,      a3,     t4
    fldx.d           f4,      a3,     t5
    vld              vr9,     a4,     0
    vldx             vr10,    a4,     s7
    vldx             vr11,    a4,     s8
    vldx             vr12,    a4,     t6
    vilvl.b          vr1,     vr0,    vr1
    vilvl.b          vr2,     vr0,    vr2
    vilvl.b          vr3,     vr0,    vr3
    vilvl.b          vr4,     vr0,    vr4
    vdp2.w.h         vr17,    vr1,    vr9
    vdp2.w.h         vr18,    vr2,    vr10
    vdp2.w.h         vr19,    vr3,    vr11
    vdp2.w.h         vr21,    vr4,    vr12
    vhaddw.d.w       vr1,     vr17,   vr17
    vhaddw.d.w       vr2,     vr18,   vr18
    vhaddw.d.w       vr3,     vr19,   vr19
    vhaddw.d.w       vr4,     vr21,   vr21
    vhaddw.q.d       vr1,     vr1,    vr1
    vhaddw.q.d       vr2,     vr2,    vr2
    vhaddw.q.d       vr3,     vr3,    vr3
    vhaddw.q.d       vr4,     vr4,    vr4
    vilvl.w          vr1,     vr2,    vr1
    vilvl.w          vr3,     vr4,    vr3
    vilvl.d          vr1,     vr3,    vr1
    vadd.w           vr22,    vr22,   vr1
    addi.w           s1,      s1,     8
    addi.d           a3,      a3,     8
    addi.d           a4,      a4,     16
    blt              s1,      s0,     .LOOP_DST
    blt              s1,      a6,     .DSTA
    b                .END_FILTERA
.DSTA:
    ld.w             t2,      a5,     0
    li.w             t3,      0
    move             s6,      s1
.FILTERA:
    add.w            t4,      t2,     t3
    ldx.bu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t6,      t6,     1
    ldx.h            t6,      a4,     t6
    mul.w            t6,      t5,     t6
    add.w            s2,      s2,     t6
    addi.w           t3,      t3,     1
    addi.w           s6,      s6,     1
    blt              s6,      a6,     .FILTERA

    ld.w             t2,      a5,     4
    li.w             t3,      0
    move             s6,      s1
    addi.w           t1,      t1,     1
.FILTERB:
    add.w            t4,      t2,     t3
    ldx.bu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t6,      t6,     1
    ldx.h            t6,      a4,     t6
    mul.w            t6,      t5,     t6
    add.w            s3,      s3,     t6
    addi.w           t3,      t3,     1
    addi.w           s6,      s6,     1
    blt              s6,      a6,     .FILTERB
    ld.w             t2,      a5,     8
    addi.w           t1,      t1,     1
    li.w             t3,      0
    move             s6,      s1
.FILTERC:
    add.w            t4,      t2,     t3
    ldx.bu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t6,      t6,     1
    ldx.h            t6,      a4,     t6
    mul.w            t6,      t5,     t6
    add.w            s4,      s4,     t6
    addi.w           t3,      t3,     1
    addi.w           s6,      s6,     1
    blt              s6,      a6,     .FILTERC
    ld.w             t2,      a5,     12
    addi.w           t1,      t1,     1
    move             s6,      s1
    li.w             t3,      0
.FILTERD:
    add.w            t4,      t2,     t3
    ldx.bu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t6,      t6,     1
    ldx.h            t6,      a4,     t6
    mul.w            t6,      t5,     t6
    add.w            s5,      s5,     t6
    addi.w           t3,      t3,     1
    addi.w           s6,      s6,     1
    blt              s6,      a6,     .FILTERD
.END_FILTERA:
    vpickve2gr.w     t1,      vr22,   0
    vpickve2gr.w     t2,      vr22,   1
    vpickve2gr.w     t3,      vr22,   2
    vpickve2gr.w     t4,      vr22,   3
    add.w            s2,      s2,     t1
    add.w            s3,      s3,     t2
    add.w            s4,      s4,     t3
    add.w            s5,      s5,     t4
    srai.w           s2,      s2,     3
    srai.w           s3,      s3,     3
    srai.w           s4,      s4,     3
    srai.w           s5,      s5,     3
    slt              t1,      s2,     t0
    slt              t2,      s3,     t0
    slt              t3,      s4,     t0
    slt              t4,      s5,     t0
    maskeqz          s2,      s2,     t1
    maskeqz          s3,      s3,     t2
    maskeqz          s4,      s4,     t3
    maskeqz          s5,      s5,     t4
    masknez          t1,      t0,     t1
    masknez          t2,      t0,     t2
    masknez          t3,      t0,     t3
    masknez          t4,      t0,     t4
    or               s2,      s2,     t1
    or               s3,      s3,     t2
    or               s4,      s4,     t3
    or               s5,      s5,     t4
    st.w             s2,      a1,     0
    st.w             s3,      a1,     4
    st.w             s4,      a1,     8
    st.w             s5,      a1,     12

    addi.d           a1,      a1,     16
    sub.d            a3,      a3,     s1
    addi.d           a5,      a5,     16
    slli.d           t3,      a6,     3
    add.d            a4,      a4,     t3
    sub.d            a4,      a4,     s1
    sub.d            a4,      a4,     s1
    addi.d           a2,      a2,     -4
    bge              a2,      t7,     .LOOP
    blt              zero,    a2,     .RESA
    b                .END
.RESA:
    li.w             t1,      0
.DST:
    slli.w           t2,      t1,     2
    ldx.w            t2,      a5,     t2
    li.w             t3,      0
    li.w             t8,      0
.FILTER:
    add.w            t4,      t2,     t3
    ldx.bu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t7,      t6,     1
    ldx.h            t7,      a4,     t7
    mul.w            t7,      t5,     t7
    add.w            t8,      t8,     t7
    addi.w           t3,      t3,     1
    blt              t3,      a6,     .FILTER
    srai.w           t8,      t8,     3
    slt              t5,      t8,     t0
    maskeqz          t8,      t8,     t5
    masknez          t5,      t0,     t5
    or               t8,      t8,     t5
    slli.w           t4,      t1,     2
    stx.w            t8,      a1,     t4
    addi.w           t1,      t1,     1
    blt              t1,      a2,     .DST
    b                .END

.LOOP_DST8:
    ld.w             t1,      a5,     0
    ld.w             t2,      a5,     4
    ld.w             t3,      a5,     8
    ld.w             t4,      a5,     12
    fldx.d           f1,      a3,     t1
    fldx.d           f2,      a3,     t2
    fldx.d           f3,      a3,     t3
    fldx.d           f4,      a3,     t4
    ld.w             t1,      a5,     16
    ld.w             t2,      a5,     20
    ld.w             t3,      a5,     24
    ld.w             t4,      a5,     28
    fldx.d           f5,      a3,     t1
    fldx.d           f6,      a3,     t2
    fldx.d           f7,      a3,     t3
    fldx.d           f8,      a3,     t4
    vld              vr9,     a4,     0
    vld              vr10,    a4,     16
    vld              vr11,    a4,     32
    vld              vr12,    a4,     48
    vld              vr13,    a4,     64
    vld              vr14,    a4,     80
    vld              vr15,    a4,     96
    vld              vr16,    a4,     112
    vilvl.b          vr1,     vr0,    vr1
    vilvl.b          vr2,     vr0,    vr2
    vilvl.b          vr3,     vr0,    vr3
    vilvl.b          vr4,     vr0,    vr4
    vilvl.b          vr5,     vr0,    vr5
    vilvl.b          vr6,     vr0,    vr6
    vilvl.b          vr7,     vr0,    vr7
    vilvl.b          vr8,     vr0,    vr8

    vdp2.w.h         vr17,    vr1,    vr9
    vdp2.w.h         vr18,    vr2,    vr10
    vdp2.w.h         vr19,    vr3,    vr11
    vdp2.w.h         vr21,    vr4,    vr12
    vdp2.w.h         vr1,     vr5,    vr13
    vdp2.w.h         vr2,     vr6,    vr14
    vdp2.w.h         vr3,     vr7,    vr15
    vdp2.w.h         vr4,     vr8,    vr16
    vhaddw.d.w       vr5,     vr1,    vr1
    vhaddw.d.w       vr6,     vr2,    vr2
    vhaddw.d.w       vr7,     vr3,    vr3
    vhaddw.d.w       vr8,     vr4,    vr4
    vhaddw.d.w       vr1,     vr17,   vr17
    vhaddw.d.w       vr2,     vr18,   vr18
    vhaddw.d.w       vr3,     vr19,   vr19
    vhaddw.d.w       vr4,     vr21,   vr21
    vhaddw.q.d       vr1,     vr1,    vr1
    vhaddw.q.d       vr2,     vr2,    vr2
    vhaddw.q.d       vr3,     vr3,    vr3
    vhaddw.q.d       vr4,     vr4,    vr4
    vhaddw.q.d       vr5,     vr5,    vr5
    vhaddw.q.d       vr6,     vr6,    vr6
    vhaddw.q.d       vr7,     vr7,    vr7
    vhaddw.q.d       vr8,     vr8,    vr8
    vilvl.w          vr1,     vr2,    vr1
    vilvl.w          vr3,     vr4,    vr3
    vilvl.w          vr5,     vr6,    vr5
    vilvl.w          vr7,     vr8,    vr7
    vilvl.d          vr1,     vr3,    vr1
    vilvl.d          vr5,     vr7,    vr5
    vsrai.w          vr1,     vr1,    3
    vsrai.w          vr5,     vr5,    3
    vmin.w           vr1,     vr1,    vr20
    vmin.w           vr5,     vr5,    vr20

    vst              vr1,     a1,     0
    vst              vr5,     a1,     16
    addi.d           a1,      a1,     32
    addi.d           a5,      a5,     32
    addi.d           a4,      a4,     128
    addi.d           a2,      a2,     -8
    bge              a2,      t8,     .LOOP_DST8
    blt              zero,    a2,     .REST8
    b                .END
.REST8:
    li.w             t1,      0
.DST8:
    slli.w           t2,      t1,     2
    ldx.w            t2,      a5,     t2
    li.w             t3,      0
    li.w             t8,      0
.FILTER8:
    add.w            t4,      t2,     t3
    ldx.bu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t7,      t6,     1
    ldx.h            t7,      a4,     t7
    mul.w            t7,      t5,     t7
    add.w            t8,      t8,     t7
    addi.w           t3,      t3,     1
    blt              t3,      a6,     .FILTER8
    srai.w           t8,      t8,     3
    slt              t5,      t8,     t0
    maskeqz          t8,      t8,     t5
    masknez          t5,      t0,     t5
    or               t8,      t8,     t5
    slli.w           t4,      t1,     2
    stx.w            t8,      a1,     t4
    addi.w           t1,      t1,     1
    blt              t1,      a2,     .DST8
    b                .END

.LOOP_DST4:
    ld.w             t1,      a5,     0
    ld.w             t2,      a5,     4
    ld.w             t3,      a5,     8
    ld.w             t4,      a5,     12
    fldx.s           f1,      a3,     t1
    fldx.s           f2,      a3,     t2
    fldx.s           f3,      a3,     t3
    fldx.s           f4,      a3,     t4
    ld.w             t1,      a5,     16
    ld.w             t2,      a5,     20
    ld.w             t3,      a5,     24
    ld.w             t4,      a5,     28
    fldx.s           f5,      a3,     t1
    fldx.s           f6,      a3,     t2
    fldx.s           f7,      a3,     t3
    fldx.s           f8,      a3,     t4
    vld              vr9,     a4,     0
    vld              vr10,    a4,     16
    vld              vr11,    a4,     32
    vld              vr12,    a4,     48
    vilvl.w          vr1,     vr2,    vr1
    vilvl.w          vr3,     vr4,    vr3
    vilvl.w          vr5,     vr6,    vr5
    vilvl.w          vr7,     vr8,    vr7
    vilvl.b          vr1,     vr0,    vr1
    vilvl.b          vr3,     vr0,    vr3
    vilvl.b          vr5,     vr0,    vr5
    vilvl.b          vr7,     vr0,    vr7

    vdp2.w.h         vr13,    vr1,    vr9
    vdp2.w.h         vr14,    vr3,    vr10
    vdp2.w.h         vr15,    vr5,    vr11
    vdp2.w.h         vr16,    vr7,    vr12
    vhaddw.d.w       vr13,    vr13,   vr13
    vhaddw.d.w       vr14,    vr14,   vr14
    vhaddw.d.w       vr15,    vr15,   vr15
    vhaddw.d.w       vr16,    vr16,   vr16
    vpickev.w        vr13,    vr14,   vr13
    vpickev.w        vr15,    vr16,   vr15
    vsrai.w          vr13,    vr13,   3
    vsrai.w          vr15,    vr15,   3
    vmin.w           vr13,    vr13,   vr20
    vmin.w           vr15,    vr15,   vr20

    vst              vr13,    a1,     0
    vst              vr15,    a1,     16
    addi.d           a1,      a1,     32
    addi.d           a5,      a5,     32
    addi.d           a4,      a4,     64
    addi.d           a2,      a2,     -8
    bge              a2,      t8,     .LOOP_DST4
    blt              zero,    a2,     .REST4
    b                .END
.REST4:
    li.w             t1,      0
.DST4:
    slli.w           t2,      t1,     2
    ldx.w            t2,      a5,     t2
    li.w             t3,      0
    li.w             t8,      0
.FILTER4:
    add.w            t4,      t2,     t3
    ldx.bu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t7,      t6,     1
    ldx.h            t7,      a4,     t7
    mul.w            t7,      t5,     t7
    add.w            t8,      t8,     t7
    addi.w           t3,      t3,     1
    blt              t3,      a6,     .FILTER4
    srai.w           t8,      t8,     3
    slt              t5,      t8,     t0
    maskeqz          t8,      t8,     t5
    masknez          t5,      t0,     t5
    or               t8,      t8,     t5
    slli.w           t4,      t1,     2
    stx.w            t8,      a1,     t4
    addi.w           t1,      t1,     1
    blt              t1,      a2,     .DST4
    b                .END
.END_DST4:

    li.w             t1,      0
.LOOP_DST1:
    slli.w           t2,      t1,     2
    ldx.w            t2,      a5,     t2
    li.w             t3,      0
    li.w             t8,      0
.FILTER1:
    add.w            t4,      t2,     t3
    ldx.bu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t7,      t6,     1
    ldx.h            t7,      a4,     t7
    mul.w            t7,      t5,     t7
    add.w            t8,      t8,     t7
    addi.w           t3,      t3,     1
    blt              t3,      a6,     .FILTER1
    srai.w           t8,      t8,     3
    slt              t5,      t8,     t0
    maskeqz          t8,      t8,     t5
    masknez          t5,      t0,     t5
    or               t8,      t8,     t5
    slli.w           t4,      t1,     2
    stx.w            t8,      a1,     t4
    addi.w           t1,      t1,     1
    blt              t1,      a2,     .LOOP_DST1
    b                .END
.END:

    ld.d             s0,      sp,     0
    ld.d             s1,      sp,     8
    ld.d             s2,      sp,     16
    ld.d             s3,      sp,     24
    ld.d             s4,      sp,     32
    ld.d             s5,      sp,     40
    ld.d             s6,      sp,     48
    ld.d             s7,      sp,     56
    ld.d             s8,      sp,     64
    addi.d           sp,      sp,     72
endfunc

/* void ff_hscale_16_to_15_sub_lsx(SwsContext *c, int16_t *dst, int dstW,
 *                                 const uint8_t *src, const int16_t *filter,
 *                                 const int32_t *filterPos, int filterSize, int sh)
 */
function ff_hscale_16_to_15_sub_lsx
    addi.d           sp,      sp,     -72
    st.d             s0,      sp,     0
    st.d             s1,      sp,     8
    st.d             s2,      sp,     16
    st.d             s3,      sp,     24
    st.d             s4,      sp,     32
    st.d             s5,      sp,     40
    st.d             s6,      sp,     48
    st.d             s7,      sp,     56
    st.d             s8,      sp,     64
    li.w             t0,      32767
    li.w             t8,      8
    li.w             t7,      4
    vreplgr2vr.w     vr20,    t0
    vreplgr2vr.w     vr0,     a7
    beq              a6,      t7,     .LOOP_HS15_DST4
    beq              a6,      t8,     .LOOP_HS15_DST8
    blt              t8,      a6,     .LOOP_HS15
    b                .END_HS15_DST4

.LOOP_HS15:
    li.w             t1,      0
    li.w             s1,      0
    li.w             s2,      0
    li.w             s3,      0
    li.w             s4,      0
    li.w             s5,      0
    vldi             vr22,    0
    addi.w           s0,      a6,     -7
    slli.w           s7,      a6,     1
    slli.w           s8,      a6,     2
    add.w            t6,      s7,     s8
.LOOP_HS15_DST:
    ld.w             t2,      a5,     0
    ld.w             t3,      a5,     4
    ld.w             t4,      a5,     8
    ld.w             t5,      a5,     12
    slli.w           t2,      t2,     1
    slli.w           t3,      t3,     1
    slli.w           t4,      t4,     1
    slli.w           t5,      t5,     1
    vldx             vr1,     a3,     t2
    vldx             vr2,     a3,     t3
    vldx             vr3,     a3,     t4
    vldx             vr4,     a3,     t5
    vld              vr9,     a4,     0
    vldx             vr10,    a4,     s7
    vldx             vr11,    a4,     s8
    vldx             vr12,    a4,     t6
    vmulwev.w.hu.h   vr17,    vr1,    vr9
    vmulwev.w.hu.h   vr18,    vr2,    vr10
    vmulwev.w.hu.h   vr19,    vr3,    vr11
    vmulwev.w.hu.h   vr21,    vr4,    vr12
    vmaddwod.w.hu.h  vr17,    vr1,    vr9
    vmaddwod.w.hu.h  vr18,    vr2,    vr10
    vmaddwod.w.hu.h  vr19,    vr3,    vr11
    vmaddwod.w.hu.h  vr21,    vr4,    vr12
    vhaddw.d.w       vr1,     vr17,   vr17
    vhaddw.d.w       vr2,     vr18,   vr18
    vhaddw.d.w       vr3,     vr19,   vr19
    vhaddw.d.w       vr4,     vr21,   vr21
    vhaddw.q.d       vr1,     vr1,    vr1
    vhaddw.q.d       vr2,     vr2,    vr2
    vhaddw.q.d       vr3,     vr3,    vr3
    vhaddw.q.d       vr4,     vr4,    vr4
    vilvl.w          vr1,     vr2,    vr1
    vilvl.w          vr3,     vr4,    vr3
    vilvl.d          vr1,     vr3,    vr1
    vadd.w           vr22,    vr22,   vr1
    addi.w           s1,      s1,     8
    addi.d           a3,      a3,     16
    addi.d           a4,      a4,     16
    blt              s1,      s0,     .LOOP_HS15_DST
    blt              s1,      a6,     .HS15_DSTA
    b                .END_HS15_FILTERA
.HS15_DSTA:
    ld.w             t2,      a5,     0
    li.w             t3,      0
    move             s6,      s1
.HS15_FILTERA:
    add.w            t4,      t2,     t3
    slli.w           t4,      t4,     1
    ldx.hu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t6,      t6,     1
    ldx.h            t6,      a4,     t6
    mul.w            t6,      t5,     t6
    add.w            s2,      s2,     t6
    addi.w           t3,      t3,     1
    addi.w           s6,      s6,     1
    blt              s6,      a6,     .HS15_FILTERA

    ld.w             t2,      a5,     4
    li.w             t3,      0
    move             s6,      s1
    addi.w           t1,      t1,     1
.HS15_FILTERB:
    add.w            t4,      t2,     t3
    slli.w           t4,      t4,     1
    ldx.hu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t6,      t6,     1
    ldx.h            t6,      a4,     t6
    mul.w            t6,      t5,     t6
    add.w            s3,      s3,     t6
    addi.w           t3,      t3,     1
    addi.w           s6,      s6,     1
    blt              s6,      a6,     .HS15_FILTERB
    ld.w             t2,      a5,     8
    addi.w           t1,      t1,     1
    li.w             t3,      0
    move             s6,      s1
.HS15_FILTERC:
    add.w            t4,      t2,     t3
    slli.w           t4,      t4,     1
    ldx.hu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t6,      t6,     1
    ldx.h            t6,      a4,     t6
    mul.w            t6,      t5,     t6
    add.w            s4,      s4,     t6
    addi.w           t3,      t3,     1
    addi.w           s6,      s6,     1
    blt              s6,      a6,     .HS15_FILTERC
    ld.w             t2,      a5,     12
    addi.w           t1,      t1,     1
    move             s6,      s1
    li.w             t3,      0
.HS15_FILTERD:
    add.w            t4,      t2,     t3
    slli.w           t4,      t4,     1
    ldx.hu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t6,      t6,     1
    ldx.h            t6,      a4,     t6
    mul.w            t6,      t5,     t6
    add.w            s5,      s5,     t6
    addi.w           t3,      t3,     1
    addi.w           s6,      s6,     1
    blt              s6,      a6,     .HS15_FILTERD
.END_HS15_FILTERA:
    vpickve2gr.w     t1,      vr22,   0
    vpickve2gr.w     t2,      vr22,   1
    vpickve2gr.w     t3,      vr22,   2
    vpickve2gr.w     t4,      vr22,   3
    add.w            s2,      s2,     t1
    add.w            s3,      s3,     t2
    add.w            s4,      s4,     t3
    add.w            s5,      s5,     t4
    sra.w            s2,      s2,     a7
    sra.w            s3,      s3,     a7
    sra.w            s4,      s4,     a7
    sra.w            s5,      s5,     a7
    slt              t1,      s2,     t0
    slt              t2,      s3,     t0
    slt              t3,      s4,     t0
    slt              t4,      s5,     t0
    maskeqz          s2,      s2,     t1
    maskeqz          s3,      s3,     t2
    maskeqz          s4,      s4,     t3
    maskeqz          s5,      s5,     t4
    masknez          t1,      t0,     t1
    masknez          t2,      t0,     t2
    masknez          t3,      t0,     t3
    masknez          t4,      t0,     t4
    or               s2,      s2,     t1
    or               s3,      s3,     t2
    or               s4,      s4,     t3
    or               s5,      s5,     t4
    st.h             s2,      a1,     0
    st.h             s3,      a1,     2
    st.h             s4,      a1,     4
    st.h             s5,      a1,     6

    addi.d           a1,      a1,     8
    sub.d            a3,      a3,     s1
    sub.d            a3,      a3,     s1
    addi.d           a5,      a5,     16
    slli.d           t3,      a6,     3
    add.d            a4,      a4,     t3
    sub.d            a4,      a4,     s1
    sub.d            a4,      a4,     s1
    addi.d           a2,      a2,     -4
    bge              a2,      t7,     .LOOP_HS15
    blt              zero,    a2,     .HS15_RESA
    b                .HS15_END
.HS15_RESA:
    li.w             t1,      0
.HS15_DST:
    slli.w           t2,      t1,     2
    ldx.w            t2,      a5,     t2
    li.w             t3,      0
    li.w             t8,      0
.HS15_FILTER:
    add.w            t4,      t2,     t3
    slli.w           t4,      t4,     1
    ldx.hu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t7,      t6,     1
    ldx.h            t7,      a4,     t7
    mul.w            t7,      t5,     t7
    add.w            t8,      t8,     t7
    addi.w           t3,      t3,     1
    blt              t3,      a6,     .HS15_FILTER
    sra.w            t8,      t8,     a7
    slt              t5,      t8,     t0
    maskeqz          t8,      t8,     t5
    masknez          t5,      t0,     t5
    or               t8,      t8,     t5
    slli.w           t4,      t1,     1
    stx.h            t8,      a1,     t4
    addi.w           t1,      t1,     1
    blt              t1,      a2,     .HS15_DST
    b                .HS15_END

.LOOP_HS15_DST8:
    ld.w             t1,      a5,     0
    ld.w             t2,      a5,     4
    ld.w             t3,      a5,     8
    ld.w             t4,      a5,     12
    slli.w           t1,      t1,     1
    slli.w           t2,      t2,     1
    slli.w           t3,      t3,     1
    slli.w           t4,      t4,     1
    vldx             vr1,     a3,     t1
    vldx             vr2,     a3,     t2
    vldx             vr3,     a3,     t3
    vldx             vr4,     a3,     t4
    ld.w             t1,      a5,     16
    ld.w             t2,      a5,     20
    ld.w             t3,      a5,     24
    ld.w             t4,      a5,     28
    slli.w           t1,      t1,     1
    slli.w           t2,      t2,     1
    slli.w           t3,      t3,     1
    slli.w           t4,      t4,     1
    vldx             vr5,     a3,     t1
    vldx             vr6,     a3,     t2
    vldx             vr7,     a3,     t3
    vldx             vr8,     a3,     t4
    vld              vr9,     a4,     0
    vld              vr10,    a4,     16
    vld              vr11,    a4,     32
    vld              vr12,    a4,     48
    vld              vr13,    a4,     64
    vld              vr14,    a4,     80
    vld              vr15,    a4,     96
    vld              vr16,    a4,     112

    vmulwev.w.hu.h   vr17,    vr1,    vr9
    vmulwev.w.hu.h   vr18,    vr2,    vr10
    vmulwev.w.hu.h   vr19,    vr3,    vr11
    vmulwev.w.hu.h   vr21,    vr4,    vr12
    vmaddwod.w.hu.h  vr17,    vr1,    vr9
    vmaddwod.w.hu.h  vr18,    vr2,    vr10
    vmaddwod.w.hu.h  vr19,    vr3,    vr11
    vmaddwod.w.hu.h  vr21,    vr4,    vr12
    vmulwev.w.hu.h   vr1,     vr5,    vr13
    vmulwev.w.hu.h   vr2,     vr6,    vr14
    vmulwev.w.hu.h   vr3,     vr7,    vr15
    vmulwev.w.hu.h   vr4,     vr8,    vr16
    vmaddwod.w.hu.h  vr1,     vr5,    vr13
    vmaddwod.w.hu.h  vr2,     vr6,    vr14
    vmaddwod.w.hu.h  vr3,     vr7,    vr15
    vmaddwod.w.hu.h  vr4,     vr8,    vr16
    vhaddw.d.w       vr5,     vr1,    vr1
    vhaddw.d.w       vr6,     vr2,    vr2
    vhaddw.d.w       vr7,     vr3,    vr3
    vhaddw.d.w       vr8,     vr4,    vr4
    vhaddw.d.w       vr1,     vr17,   vr17
    vhaddw.d.w       vr2,     vr18,   vr18
    vhaddw.d.w       vr3,     vr19,   vr19
    vhaddw.d.w       vr4,     vr21,   vr21
    vhaddw.q.d       vr1,     vr1,    vr1
    vhaddw.q.d       vr2,     vr2,    vr2
    vhaddw.q.d       vr3,     vr3,    vr3
    vhaddw.q.d       vr4,     vr4,    vr4
    vhaddw.q.d       vr5,     vr5,    vr5
    vhaddw.q.d       vr6,     vr6,    vr6
    vhaddw.q.d       vr7,     vr7,    vr7
    vhaddw.q.d       vr8,     vr8,    vr8
    vilvl.w          vr1,     vr2,    vr1
    vilvl.w          vr3,     vr4,    vr3
    vilvl.w          vr5,     vr6,    vr5
    vilvl.w          vr7,     vr8,    vr7
    vilvl.d          vr1,     vr3,    vr1
    vilvl.d          vr5,     vr7,    vr5
    vsra.w           vr1,     vr1,    vr0
    vsra.w           vr5,     vr5,    vr0
    vmin.w           vr1,     vr1,    vr20
    vmin.w           vr5,     vr5,    vr20

    vpickev.h        vr1,     vr5,    vr1
    vst              vr1,     a1,     0
    addi.d           a1,      a1,     16
    addi.d           a5,      a5,     32
    addi.d           a4,      a4,     128
    addi.d           a2,      a2,     -8
    bge              a2,      t8,     .LOOP_HS15_DST8
    blt              zero,    a2,     .HS15_REST8
    b                .HS15_END
.HS15_REST8:
    li.w             t1,      0
.HS15_DST8:
    slli.w           t2,      t1,     2
    ldx.w            t2,      a5,     t2
    li.w             t3,      0
    li.w             t8,      0
.HS15_FILTER8:
    add.w            t4,      t2,     t3
    slli.w           t4,      t4,     1
    ldx.hu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t7,      t6,     1
    ldx.h            t7,      a4,     t7
    mul.w            t7,      t5,     t7
    add.w            t8,      t8,     t7
    addi.w           t3,      t3,     1
    blt              t3,      a6,     .HS15_FILTER8
    sra.w            t8,      t8,     a7
    slt              t5,      t8,     t0
    maskeqz          t8,      t8,     t5
    masknez          t5,      t0,     t5
    or               t8,      t8,     t5
    slli.w           t4,      t1,     1
    stx.h            t8,      a1,     t4
    addi.w           t1,      t1,     1
    blt              t1,      a2,     .HS15_DST8
    b                .HS15_END

.LOOP_HS15_DST4:
    ld.w             t1,      a5,     0
    ld.w             t2,      a5,     4
    ld.w             t3,      a5,     8
    ld.w             t4,      a5,     12
    slli.w           t1,      t1,     1
    slli.w           t2,      t2,     1
    slli.w           t3,      t3,     1
    slli.w           t4,      t4,     1
    fldx.d           f1,      a3,     t1
    fldx.d           f2,      a3,     t2
    fldx.d           f3,      a3,     t3
    fldx.d           f4,      a3,     t4
    ld.w             t1,      a5,     16
    ld.w             t2,      a5,     20
    ld.w             t3,      a5,     24
    ld.w             t4,      a5,     28
    slli.w           t1,      t1,     1
    slli.w           t2,      t2,     1
    slli.w           t3,      t3,     1
    slli.w           t4,      t4,     1
    fldx.d           f5,      a3,     t1
    fldx.d           f6,      a3,     t2
    fldx.d           f7,      a3,     t3
    fldx.d           f8,      a3,     t4
    vld              vr9,     a4,     0
    vld              vr10,    a4,     16
    vld              vr11,    a4,     32
    vld              vr12,    a4,     48
    vilvl.d          vr1,     vr2,    vr1
    vilvl.d          vr3,     vr4,    vr3
    vilvl.d          vr5,     vr6,    vr5
    vilvl.d          vr7,     vr8,    vr7
    vmulwev.w.hu.h   vr13,    vr1,    vr9
    vmulwev.w.hu.h   vr14,    vr3,    vr10
    vmulwev.w.hu.h   vr15,    vr5,    vr11
    vmulwev.w.hu.h   vr16,    vr7,    vr12
    vmaddwod.w.hu.h  vr13,    vr1,    vr9
    vmaddwod.w.hu.h  vr14,    vr3,    vr10
    vmaddwod.w.hu.h  vr15,    vr5,    vr11
    vmaddwod.w.hu.h  vr16,    vr7,    vr12
    vhaddw.d.w       vr13,    vr13,   vr13
    vhaddw.d.w       vr14,    vr14,   vr14
    vhaddw.d.w       vr15,    vr15,   vr15
    vhaddw.d.w       vr16,    vr16,   vr16
    vpickev.w        vr13,    vr14,   vr13
    vpickev.w        vr15,    vr16,   vr15
    vsra.w           vr13,    vr13,   vr0
    vsra.w           vr15,    vr15,   vr0
    vmin.w           vr13,    vr13,   vr20
    vmin.w           vr15,    vr15,   vr20

    vpickev.h        vr13,    vr15,   vr13
    vst              vr13,    a1,     0
    addi.d           a1,      a1,     16
    addi.d           a5,      a5,     32
    addi.d           a4,      a4,     64
    addi.d           a2,      a2,     -8
    bge              a2,      t8,     .LOOP_HS15_DST4
    blt              zero,    a2,     .HS15_REST4
    b                .HS15_END
.HS15_REST4:
    li.w             t1,      0
.HS15_DST4:
    slli.w           t2,      t1,     2
    ldx.w            t2,      a5,     t2
    li.w             t3,      0
    li.w             t8,      0
.HS15_FILTER4:
    add.w            t4,      t2,     t3
    slli.w           t4,      t4,     1
    ldx.hu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t7,      t6,     1
    ldx.h            t7,      a4,     t7
    mul.w            t7,      t5,     t7
    add.w            t8,      t8,     t7
    addi.w           t3,      t3,     1
    blt              t3,      a6,     .HS15_FILTER4
    sra.w            t8,      t8,     a7
    slt              t5,      t8,     t0
    maskeqz          t8,      t8,     t5
    masknez          t5,      t0,     t5
    or               t8,      t8,     t5
    slli.w           t4,      t1,     1
    stx.h            t8,      a1,     t4
    addi.w           t1,      t1,     1
    blt              t1,      a2,     .HS15_DST4
    b                .HS15_END
.END_HS15_DST4:

    li.w             t1,      0
.LOOP_HS15_DST1:
    slli.w           t2,      t1,     2
    ldx.w            t2,      a5,     t2
    li.w             t3,      0
    li.w             t8,      0
.HS15_FILTER1:
    add.w            t4,      t2,     t3
    slli.w           t4,      t4,     1
    ldx.hu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t7,      t6,     1
    ldx.h            t7,      a4,     t7
    mul.w            t7,      t5,     t7
    add.w            t8,      t8,     t7
    addi.w           t3,      t3,     1
    blt              t3,      a6,     .HS15_FILTER1
    sra.w            t8,      t8,     a7
    slt              t5,      t8,     t0
    maskeqz          t8,      t8,     t5
    masknez          t5,      t0,     t5
    or               t8,      t8,     t5
    slli.w           t4,      t1,     1
    stx.h            t8,      a1,     t4
    addi.w           t1,      t1,     1
    blt              t1,      a2,     .LOOP_HS15_DST1
    b                .HS15_END
.HS15_END:

    ld.d             s0,      sp,     0
    ld.d             s1,      sp,     8
    ld.d             s2,      sp,     16
    ld.d             s3,      sp,     24
    ld.d             s4,      sp,     32
    ld.d             s5,      sp,     40
    ld.d             s6,      sp,     48
    ld.d             s7,      sp,     56
    ld.d             s8,      sp,     64
    addi.d           sp,      sp,     72
endfunc

/* void ff_hscale_16_to_19_sub_lsx(SwsContext *c, int16_t *dst, int dstW,
 *                                 const uint8_t *src, const int16_t *filter,
 *                                 const int32_t *filterPos, int filterSize, int sh)
 */
function ff_hscale_16_to_19_sub_lsx
    addi.d           sp,      sp,     -72
    st.d             s0,      sp,     0
    st.d             s1,      sp,     8
    st.d             s2,      sp,     16
    st.d             s3,      sp,     24
    st.d             s4,      sp,     32
    st.d             s5,      sp,     40
    st.d             s6,      sp,     48
    st.d             s7,      sp,     56
    st.d             s8,      sp,     64

    li.w             t0,      524287
    li.w             t8,      8
    li.w             t7,      4
    vreplgr2vr.w     vr20,    t0
    vreplgr2vr.w     vr0,     a7
    beq              a6,      t7,     .LOOP_HS19_DST4
    beq              a6,      t8,     .LOOP_HS19_DST8
    blt              t8,      a6,     .LOOP_HS19
    b                .END_HS19_DST4

.LOOP_HS19:
    li.w             t1,      0
    li.w             s1,      0
    li.w             s2,      0
    li.w             s3,      0
    li.w             s4,      0
    li.w             s5,      0
    vldi             vr22,    0
    addi.w           s0,      a6,     -7
    slli.w           s7,      a6,     1
    slli.w           s8,      a6,     2
    add.w            t6,      s7,     s8
.LOOP_HS19_DST:
    ld.w             t2,      a5,     0
    ld.w             t3,      a5,     4
    ld.w             t4,      a5,     8
    ld.w             t5,      a5,     12
    slli.w           t2,      t2,     1
    slli.w           t3,      t3,     1
    slli.w           t4,      t4,     1
    slli.w           t5,      t5,     1
    vldx             vr1,     a3,     t2
    vldx             vr2,     a3,     t3
    vldx             vr3,     a3,     t4
    vldx             vr4,     a3,     t5
    vld              vr9,     a4,     0
    vldx             vr10,    a4,     s7
    vldx             vr11,    a4,     s8
    vldx             vr12,    a4,     t6
    vmulwev.w.hu.h   vr17,    vr1,    vr9
    vmulwev.w.hu.h   vr18,    vr2,    vr10
    vmulwev.w.hu.h   vr19,    vr3,    vr11
    vmulwev.w.hu.h   vr21,    vr4,    vr12
    vmaddwod.w.hu.h  vr17,    vr1,    vr9
    vmaddwod.w.hu.h  vr18,    vr2,    vr10
    vmaddwod.w.hu.h  vr19,    vr3,    vr11
    vmaddwod.w.hu.h  vr21,    vr4,    vr12
    vhaddw.d.w       vr1,     vr17,   vr17
    vhaddw.d.w       vr2,     vr18,   vr18
    vhaddw.d.w       vr3,     vr19,   vr19
    vhaddw.d.w       vr4,     vr21,   vr21
    vhaddw.q.d       vr1,     vr1,    vr1
    vhaddw.q.d       vr2,     vr2,    vr2
    vhaddw.q.d       vr3,     vr3,    vr3
    vhaddw.q.d       vr4,     vr4,    vr4
    vilvl.w          vr1,     vr2,    vr1
    vilvl.w          vr3,     vr4,    vr3
    vilvl.d          vr1,     vr3,    vr1
    vadd.w           vr22,    vr22,   vr1
    addi.w           s1,      s1,     8
    addi.d           a3,      a3,     16
    addi.d           a4,      a4,     16
    blt              s1,      s0,     .LOOP_HS19_DST
    blt              s1,      a6,     .HS19_DSTA
    b                .END_HS19_FILTERA
.HS19_DSTA:
    ld.w             t2,      a5,     0
    li.w             t3,      0
    move             s6,      s1
.HS19_FILTERA:
    add.w            t4,      t2,     t3
    slli.w           t4,      t4,     1
    ldx.hu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t6,      t6,     1
    ldx.h            t6,      a4,     t6
    mul.w            t6,      t5,     t6
    add.w            s2,      s2,     t6
    addi.w           t3,      t3,     1
    addi.w           s6,      s6,     1
    blt              s6,      a6,     .HS19_FILTERA

    ld.w             t2,      a5,     4
    li.w             t3,      0
    move             s6,      s1
    addi.w           t1,      t1,     1
.HS19_FILTERB:
    add.w            t4,      t2,     t3
    slli.w           t4,      t4,     1
    ldx.hu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t6,      t6,     1
    ldx.h            t6,      a4,     t6
    mul.w            t6,      t5,     t6
    add.w            s3,      s3,     t6
    addi.w           t3,      t3,     1
    addi.w           s6,      s6,     1
    blt              s6,      a6,     .HS19_FILTERB
    ld.w             t2,      a5,     8
    addi.w           t1,      t1,     1
    li.w             t3,      0
    move             s6,      s1
.HS19_FILTERC:
    add.w            t4,      t2,     t3
    slli.w           t4,      t4,     1
    ldx.hu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t6,      t6,     1
    ldx.h            t6,      a4,     t6
    mul.w            t6,      t5,     t6
    add.w            s4,      s4,     t6
    addi.w           t3,      t3,     1
    addi.w           s6,      s6,     1
    blt              s6,      a6,     .HS19_FILTERC
    ld.w             t2,      a5,     12
    addi.w           t1,      t1,     1
    move             s6,      s1
    li.w             t3,      0
.HS19_FILTERD:
    add.w            t4,      t2,     t3
    slli.w           t4,      t4,     1
    ldx.hu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t6,      t6,     1
    ldx.h            t6,      a4,     t6
    mul.w            t6,      t5,     t6
    add.w            s5,      s5,     t6
    addi.w           t3,      t3,     1
    addi.w           s6,      s6,     1
    blt              s6,      a6,     .HS19_FILTERD
.END_HS19_FILTERA:
    vpickve2gr.w     t1,      vr22,   0
    vpickve2gr.w     t2,      vr22,   1
    vpickve2gr.w     t3,      vr22,   2
    vpickve2gr.w     t4,      vr22,   3
    add.w            s2,      s2,     t1
    add.w            s3,      s3,     t2
    add.w            s4,      s4,     t3
    add.w            s5,      s5,     t4
    sra.w            s2,      s2,     a7
    sra.w            s3,      s3,     a7
    sra.w            s4,      s4,     a7
    sra.w            s5,      s5,     a7
    slt              t1,      s2,     t0
    slt              t2,      s3,     t0
    slt              t3,      s4,     t0
    slt              t4,      s5,     t0
    maskeqz          s2,      s2,     t1
    maskeqz          s3,      s3,     t2
    maskeqz          s4,      s4,     t3
    maskeqz          s5,      s5,     t4
    masknez          t1,      t0,     t1
    masknez          t2,      t0,     t2
    masknez          t3,      t0,     t3
    masknez          t4,      t0,     t4
    or               s2,      s2,     t1
    or               s3,      s3,     t2
    or               s4,      s4,     t3
    or               s5,      s5,     t4
    st.w             s2,      a1,     0
    st.w             s3,      a1,     4
    st.w             s4,      a1,     8
    st.w             s5,      a1,     12

    addi.d           a1,      a1,     16
    sub.d            a3,      a3,     s1
    sub.d            a3,      a3,     s1
    addi.d           a5,      a5,     16
    slli.d           t3,      a6,     3
    add.d            a4,      a4,     t3
    sub.d            a4,      a4,     s1
    sub.d            a4,      a4,     s1
    addi.d           a2,      a2,     -4
    bge              a2,      t7,     .LOOP_HS19
    blt              zero,    a2,     .HS19_RESA
    b                .HS19_END
.HS19_RESA:
    li.w             t1,      0
.HS19_DST:
    slli.w           t2,      t1,     2
    ldx.w            t2,      a5,     t2
    li.w             t3,      0
    li.w             t8,      0
.HS19_FILTER:
    add.w            t4,      t2,     t3
    slli.w           t4,      t4,     1
    ldx.hu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t7,      t6,     1
    ldx.h            t7,      a4,     t7
    mul.w            t7,      t5,     t7
    add.w            t8,      t8,     t7
    addi.w           t3,      t3,     1
    blt              t3,      a6,     .HS19_FILTER
    sra.w            t8,      t8,     a7
    slt              t5,      t8,     t0
    maskeqz          t8,      t8,     t5
    masknez          t5,      t0,     t5
    or               t8,      t8,     t5
    slli.w           t4,      t1,     2
    stx.w            t8,      a1,     t4
    addi.w           t1,      t1,     1
    blt              t1,      a2,     .HS19_DST
    b                .HS19_END

.LOOP_HS19_DST8:
    ld.w             t1,      a5,     0
    ld.w             t2,      a5,     4
    ld.w             t3,      a5,     8
    ld.w             t4,      a5,     12
    slli.w           t1,      t1,     1
    slli.w           t2,      t2,     1
    slli.w           t3,      t3,     1
    slli.w           t4,      t4,     1
    vldx             vr1,     a3,     t1
    vldx             vr2,     a3,     t2
    vldx             vr3,     a3,     t3
    vldx             vr4,     a3,     t4
    ld.w             t1,      a5,     16
    ld.w             t2,      a5,     20
    ld.w             t3,      a5,     24
    ld.w             t4,      a5,     28
    slli.w           t1,      t1,     1
    slli.w           t2,      t2,     1
    slli.w           t3,      t3,     1
    slli.w           t4,      t4,     1
    vldx             vr5,     a3,     t1
    vldx             vr6,     a3,     t2
    vldx             vr7,     a3,     t3
    vldx             vr8,     a3,     t4
    vld              vr9,     a4,     0
    vld              vr10,    a4,     16
    vld              vr11,    a4,     32
    vld              vr12,    a4,     48
    vld              vr13,    a4,     64
    vld              vr14,    a4,     80
    vld              vr15,    a4,     96
    vld              vr16,    a4,     112
    vmulwev.w.hu.h   vr17,    vr1,    vr9
    vmulwev.w.hu.h   vr18,    vr2,    vr10
    vmulwev.w.hu.h   vr19,    vr3,    vr11
    vmulwev.w.hu.h   vr21,    vr4,    vr12
    vmaddwod.w.hu.h  vr17,    vr1,    vr9
    vmaddwod.w.hu.h  vr18,    vr2,    vr10
    vmaddwod.w.hu.h  vr19,    vr3,    vr11
    vmaddwod.w.hu.h  vr21,    vr4,    vr12
    vmulwev.w.hu.h   vr1,     vr5,    vr13
    vmulwev.w.hu.h   vr2,     vr6,    vr14
    vmulwev.w.hu.h   vr3,     vr7,    vr15
    vmulwev.w.hu.h   vr4,     vr8,    vr16
    vmaddwod.w.hu.h  vr1,     vr5,    vr13
    vmaddwod.w.hu.h  vr2,     vr6,    vr14
    vmaddwod.w.hu.h  vr3,     vr7,    vr15
    vmaddwod.w.hu.h  vr4,     vr8,    vr16
    vhaddw.d.w       vr5,     vr1,    vr1
    vhaddw.d.w       vr6,     vr2,    vr2
    vhaddw.d.w       vr7,     vr3,    vr3
    vhaddw.d.w       vr8,     vr4,    vr4
    vhaddw.d.w       vr1,     vr17,   vr17
    vhaddw.d.w       vr2,     vr18,   vr18
    vhaddw.d.w       vr3,     vr19,   vr19
    vhaddw.d.w       vr4,     vr21,   vr21
    vhaddw.q.d       vr1,     vr1,    vr1
    vhaddw.q.d       vr2,     vr2,    vr2
    vhaddw.q.d       vr3,     vr3,    vr3
    vhaddw.q.d       vr4,     vr4,    vr4
    vhaddw.q.d       vr5,     vr5,    vr5
    vhaddw.q.d       vr6,     vr6,    vr6
    vhaddw.q.d       vr7,     vr7,    vr7
    vhaddw.q.d       vr8,     vr8,    vr8
    vilvl.w          vr1,     vr2,    vr1
    vilvl.w          vr3,     vr4,    vr3
    vilvl.w          vr5,     vr6,    vr5
    vilvl.w          vr7,     vr8,    vr7
    vilvl.d          vr1,     vr3,    vr1
    vilvl.d          vr5,     vr7,    vr5
    vsra.w           vr1,     vr1,    vr0
    vsra.w           vr5,     vr5,    vr0
    vmin.w           vr1,     vr1,    vr20
    vmin.w           vr5,     vr5,    vr20

    vst              vr1,     a1,     0
    vst              vr5,     a1,     16
    addi.d           a1,      a1,     32
    addi.d           a5,      a5,     32
    addi.d           a4,      a4,     128
    addi.d           a2,      a2,     -8
    bge              a2,      t8,     .LOOP_HS19_DST8
    blt              zero,    a2,     .HS19_REST8
    b                .HS19_END
.HS19_REST8:
    li.w             t1,      0
.HS19_DST8:
    slli.w           t2,      t1,     2
    ldx.w            t2,      a5,     t2
    li.w             t3,      0
    li.w             t8,      0
.HS19_FILTER8:
    add.w            t4,      t2,     t3
    slli.w           t4,      t4,     1
    ldx.hu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t7,      t6,     1
    ldx.h            t7,      a4,     t7
    mul.w            t7,      t5,     t7
    add.w            t8,      t8,     t7
    addi.w           t3,      t3,     1
    blt              t3,      a6,     .HS19_FILTER8
    sra.w            t8,      t8,     a7
    slt              t5,      t8,     t0
    maskeqz          t8,      t8,     t5
    masknez          t5,      t0,     t5
    or               t8,      t8,     t5
    slli.w           t4,      t1,     2
    stx.w            t8,      a1,     t4
    addi.w           t1,      t1,     1
    blt              t1,      a2,     .HS19_DST8
    b                .HS19_END

.LOOP_HS19_DST4:
    ld.w             t1,      a5,     0
    ld.w             t2,      a5,     4
    ld.w             t3,      a5,     8
    ld.w             t4,      a5,     12
    slli.w           t1,      t1,     1
    slli.w           t2,      t2,     1
    slli.w           t3,      t3,     1
    slli.w           t4,      t4,     1
    fldx.d           f1,      a3,     t1
    fldx.d           f2,      a3,     t2
    fldx.d           f3,      a3,     t3
    fldx.d           f4,      a3,     t4
    ld.w             t1,      a5,     16
    ld.w             t2,      a5,     20
    ld.w             t3,      a5,     24
    ld.w             t4,      a5,     28
    slli.w           t1,      t1,     1
    slli.w           t2,      t2,     1
    slli.w           t3,      t3,     1
    slli.w           t4,      t4,     1
    fldx.d           f5,      a3,     t1
    fldx.d           f6,      a3,     t2
    fldx.d           f7,      a3,     t3
    fldx.d           f8,      a3,     t4
    vld              vr9,     a4,     0
    vld              vr10,    a4,     16
    vld              vr11,    a4,     32
    vld              vr12,    a4,     48
    vilvl.d          vr1,     vr2,    vr1
    vilvl.d          vr3,     vr4,    vr3
    vilvl.d          vr5,     vr6,    vr5
    vilvl.d          vr7,     vr8,    vr7
    vmulwev.w.hu.h   vr13,    vr1,    vr9
    vmulwev.w.hu.h   vr14,    vr3,    vr10
    vmulwev.w.hu.h   vr15,    vr5,    vr11
    vmulwev.w.hu.h   vr16,    vr7,    vr12
    vmaddwod.w.hu.h  vr13,    vr1,    vr9
    vmaddwod.w.hu.h  vr14,    vr3,    vr10
    vmaddwod.w.hu.h  vr15,    vr5,    vr11
    vmaddwod.w.hu.h  vr16,    vr7,    vr12
    vhaddw.d.w       vr13,    vr13,   vr13
    vhaddw.d.w       vr14,    vr14,   vr14
    vhaddw.d.w       vr15,    vr15,   vr15
    vhaddw.d.w       vr16,    vr16,   vr16
    vpickev.w        vr13,    vr14,   vr13
    vpickev.w        vr15,    vr16,   vr15
    vsra.w           vr13,    vr13,   vr0
    vsra.w           vr15,    vr15,   vr0
    vmin.w           vr13,    vr13,   vr20
    vmin.w           vr15,    vr15,   vr20

    vst              vr13,    a1,     0
    vst              vr15,    a1,     16
    addi.d           a1,      a1,     32
    addi.d           a5,      a5,     32
    addi.d           a4,      a4,     64
    addi.d           a2,      a2,     -8
    bge              a2,      t8,     .LOOP_HS19_DST4
    blt              zero,    a2,     .HS19_REST4
    b                .HS19_END
.HS19_REST4:
    li.w             t1,      0
.HS19_DST4:
    slli.w           t2,      t1,     2
    ldx.w            t2,      a5,     t2
    li.w             t3,      0
    li.w             t8,      0
.HS19_FILTER4:
    add.w            t4,      t2,     t3
    slli.w           t4,      t4,     1
    ldx.hu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t7,      t6,     1
    ldx.h            t7,      a4,     t7
    mul.w            t7,      t5,     t7
    add.w            t8,      t8,     t7
    addi.w           t3,      t3,     1
    blt              t3,      a6,     .HS19_FILTER4
    sra.w            t8,      t8,     a7
    slt              t5,      t8,     t0
    maskeqz          t8,      t8,     t5
    masknez          t5,      t0,     t5
    or               t8,      t8,     t5
    slli.w           t4,      t1,     2
    stx.w            t8,      a1,     t4
    addi.w           t1,      t1,     1
    blt              t1,      a2,     .HS19_DST4
    b                .HS19_END
.END_HS19_DST4:

    li.w             t1,      0
.LOOP_HS19_DST1:
    slli.w           t2,      t1,     2
    ldx.w            t2,      a5,     t2
    li.w             t3,      0
    li.w             t8,      0
.HS19_FILTER1:
    add.w            t4,      t2,     t3
    slli.w           t4,      t4,     1
    ldx.hu           t5,      a3,     t4
    mul.w            t6,      a6,     t1
    add.w            t6,      t6,     t3
    slli.w           t7,      t6,     1
    ldx.h            t7,      a4,     t7
    mul.w            t7,      t5,     t7
    add.w            t8,      t8,     t7
    addi.w           t3,      t3,     1
    blt              t3,      a6,     .HS19_FILTER1
    sra.w            t8,      t8,     a7
    slt              t5,      t8,     t0
    maskeqz          t8,      t8,     t5
    masknez          t5,      t0,     t5
    or               t8,      t8,     t5
    slli.w           t4,      t1,     2
    stx.w            t8,      a1,     t4
    addi.w           t1,      t1,     1
    blt              t1,      a2,     .LOOP_HS19_DST1
    b                .HS19_END
.HS19_END:

    ld.d             s0,      sp,     0
    ld.d             s1,      sp,     8
    ld.d             s2,      sp,     16
    ld.d             s3,      sp,     24
    ld.d             s4,      sp,     32
    ld.d             s5,      sp,     40
    ld.d             s6,      sp,     48
    ld.d             s7,      sp,     56
    ld.d             s8,      sp,     64
    addi.d           sp,      sp,     72
endfunc

function lumRangeFromJpeg_lsx
    li.w          t0,    14071
    li.w          t1,    33561947
    vreplgr2vr.h  vr0,   t0
    srli.w        t2,    a1,    3
    andi          t3,    a1,    7
    beqz          t2,    2f
1:
    vld           vr1,   a0,    0
    vreplgr2vr.w  vr2,   t1
    vreplgr2vr.w  vr3,   t1
    vmaddwev.w.h  vr2,   vr0,   vr1
    vmaddwod.w.h  vr3,   vr0,   vr1
    vsrai.w       vr2,   vr2,   14
    vsrai.w       vr3,   vr3,   14
    vpackev.h     vr1,   vr3,   vr2
    vst           vr1,   a0,    0
    addi.d        a0,    a0,    16
    addi.d        t2,    t2,    -1
    bnez          t2,    1b
2:
    beqz          t3,    4f
3:
    ld.h          t4,    a0,    0
    mul.w         t4,    t4,    t0
    add.w         t4,    t4,    t1
    srai.w        t4,    t4,    14
    st.h          t4,    a0,    0
    addi.d        a0,    a0,    2
    addi.d        t3,    t3,    -1
    bnez          t3,    3b
4:
endfunc

function lumRangeFromJpeg_lasx
    li.w           t0,    14071
    li.w           t1,    33561947
    xvreplgr2vr.h  xr0,   t0
    srli.w         t2,    a1,    4
    andi           t3,    a1,    15
    beqz           t2,    2f
1:
    xvld           xr1,   a0,    0
    xvreplgr2vr.w  xr2,   t1
    xvreplgr2vr.w  xr3,   t1
    xvmaddwev.w.h  xr2,   xr0,   xr1
    xvmaddwod.w.h  xr3,   xr0,   xr1
    xvsrai.w       xr2,   xr2,   14
    xvsrai.w       xr3,   xr3,   14
    xvpackev.h     xr1,   xr3,   xr2
    xvst           xr1,   a0,    0
    addi.d         a0,    a0,    32
    addi.d         t2,    t2,    -1
    bnez           t2,    1b
2:
    beqz          t3,    4f
3:
    ld.h          t4,    a0,    0
    mul.w         t4,    t4,    t0
    add.w         t4,    t4,    t1
    srai.w        t4,    t4,    14
    st.h          t4,    a0,    0
    addi.d        a0,    a0,    2
    addi.d        t3,    t3,    -1
    bnez          t3,    3b
4:
endfunc

function lumRangeToJpeg_lsx
    li.w          t0,    19077
    li.w          t1,    -39057361
    li.w          t2,    30189
    vreplgr2vr.h  vr0,   t0
    vreplgr2vr.h  vr4,   t2
    srli.w        t2,    a1,    3
    andi          t3,    a1,    7
    beqz          t2,    2f
1:
    vld           vr1,   a0,    0
    vreplgr2vr.w  vr2,   t1
    vreplgr2vr.w  vr3,   t1
    vmin.h        vr1,   vr1,   vr4
    vmaddwev.w.h  vr2,   vr0,   vr1
    vmaddwod.w.h  vr3,   vr0,   vr1
    vsrai.w       vr2,   vr2,   14
    vsrai.w       vr3,   vr3,   14
    vpackev.h     vr1,   vr3,   vr2
    vst           vr1,   a0,    0
    addi.d        a0,    a0,    16
    addi.d        t2,    t2,    -1
    bnez          t2,    1b
2:
    beqz          t3,    4f
3:
    ld.h          t4,    a0,    0
    vreplgr2vr.h  vr1,   t4
    vmin.h        vr1,   vr1,   vr4
    vpickve2gr.h  t4,    vr1,   0
    mul.w         t4,    t4,    t0
    add.w         t4,    t4,    t1
    srai.w        t4,    t4,    14
    st.h          t4,    a0,    0
    addi.d        a0,    a0,    2
    addi.d        t3,    t3,    -1
    bnez          t3,    3b
4:
endfunc

function lumRangeToJpeg_lasx
    li.w           t0,    19077
    li.w           t1,    -39057361
    li.w           t2,    30189
    xvreplgr2vr.h  xr0,   t0
    xvreplgr2vr.h  xr4,   t2
    srli.w         t2,    a1,    4
    andi           t3,    a1,    15
    beqz           t2,    2f
1:
    xvld           xr1,   a0,    0
    xvreplgr2vr.w  xr2,   t1
    xvreplgr2vr.w  xr3,   t1
    xvmin.h        xr1,   xr1,   xr4
    xvmaddwev.w.h  xr2,   xr0,   xr1
    xvmaddwod.w.h  xr3,   xr0,   xr1
    xvsrai.w       xr2,   xr2,   14
    xvsrai.w       xr3,   xr3,   14
    xvpackev.h     xr1,   xr3,   xr2
    xvst           xr1,   a0,    0
    addi.d         a0,    a0,    32
    addi.d         t2,    t2,    -1
    bnez           t2,    1b
2:
    beqz           t3,    4f
3:
    ld.h           t4,    a0,    0
    vreplgr2vr.h   vr1,   t4
    vmin.h         vr1,   vr1,   vr4
    vpickve2gr.h   t4,    vr1,   0
    mul.w          t4,    t4,    t0
    add.w          t4,    t4,    t1
    srai.w         t4,    t4,    14
    st.h           t4,    a0,    0
    addi.d         a0,    a0,    2
    addi.d         t3,    t3,    -1
    bnez           t3,    3b
4:
endfunc

function chrRangeFromJpeg_lsx
    li.w          t0,    1799
    li.w          t1,    4081085
    vreplgr2vr.h  vr0,   t0
    srli.w        t2,    a2,    3
    andi          t3,    a2,    7
    beqz          t2,    2f
1:
    vld           vr1,   a0,    0
    vld           vr2,   a1,    0
    vreplgr2vr.w  vr3,   t1
    vreplgr2vr.w  vr4,   t1
    vreplgr2vr.w  vr5,   t1
    vreplgr2vr.w  vr6,   t1
    vmaddwev.w.h  vr3,   vr0,   vr1
    vmaddwod.w.h  vr4,   vr0,   vr1
    vmaddwev.w.h  vr5,   vr0,   vr2
    vmaddwod.w.h  vr6,   vr0,   vr2
    vsrai.w       vr3,   vr3,   11
    vsrai.w       vr4,   vr4,   11
    vsrai.w       vr5,   vr5,   11
    vsrai.w       vr6,   vr6,   11
    vpackev.h     vr1,   vr4,   vr3
    vpackev.h     vr2,   vr6,   vr5
    vst           vr1,   a0,    0
    vst           vr2,   a1,    0
    addi.d        a0,    a0,    16
    addi.d        a1,    a1,    16
    addi.d        t2,    t2,    -1
    bnez          t2,    1b
2:
    beqz          t3,    4f
3:
    ld.h          t4,    a0,    0
    ld.h          t5,    a1,    0
    mul.w         t4,    t4,    t0
    mul.w         t5,    t5,    t0
    add.w         t4,    t4,    t1
    add.w         t5,    t5,    t1
    srai.w        t4,    t4,    11
    srai.w        t5,    t5,    11
    st.h          t4,    a0,    0
    st.h          t5,    a1,    0
    addi.d        a0,    a0,    2
    addi.d        a1,    a1,    2
    addi.d        t3,    t3,    -1
    bnez          t3,    3b
4:
endfunc

function chrRangeFromJpeg_lasx
    li.w           t0,    1799
    li.w           t1,    4081085
    xvreplgr2vr.h  xr0,   t0
    srli.w         t2,    a2,    4
    andi           t3,    a2,    15
    beqz           t2,    2f
1:
    xvld           xr1,   a0,    0
    xvld           xr2,   a1,    0
    xvreplgr2vr.w  xr3,   t1
    xvreplgr2vr.w  xr4,   t1
    xvreplgr2vr.w  xr5,   t1
    xvreplgr2vr.w  xr6,   t1
    xvmaddwev.w.h  xr3,   xr0,   xr1
    xvmaddwod.w.h  xr4,   xr0,   xr1
    xvmaddwev.w.h  xr5,   xr0,   xr2
    xvmaddwod.w.h  xr6,   xr0,   xr2
    xvsrai.w       xr3,   xr3,   11
    xvsrai.w       xr4,   xr4,   11
    xvsrai.w       xr5,   xr5,   11
    xvsrai.w       xr6,   xr6,   11
    xvpackev.h     xr1,   xr4,   xr3
    xvpackev.h     xr2,   xr6,   xr5
    xvst           xr1,   a0,    0
    xvst           xr2,   a1,    0
    addi.d         a0,    a0,    32
    addi.d         a1,    a1,    32
    addi.d         t2,    t2,    -1
    bnez           t2,    1b
2:
    beqz          t3,    4f
3:
    ld.h          t4,    a0,    0
    ld.h          t5,    a1,    0
    mul.w         t4,    t4,    t0
    mul.w         t5,    t5,    t0
    add.w         t4,    t4,    t1
    add.w         t5,    t5,    t1
    srai.w        t4,    t4,    11
    srai.w        t5,    t5,    11
    st.h          t4,    a0,    0
    st.h          t5,    a1,    0
    addi.d        a0,    a0,    2
    addi.d        a1,    a1,    2
    addi.d        t3,    t3,    -1
    bnez          t3,    3b
4:
endfunc

function chrRangeToJpeg_lsx
    li.w          t0,    4663
    li.w          t1,    -9289992
    li.w          t2,    30775
    vreplgr2vr.h  vr0,   t0
    vreplgr2vr.h  vr7,   t2
    srli.w        t2,    a2,    3
    andi          t3,    a2,    7
    beqz          t2,    2f
1:
    vld           vr1,   a0,    0
    vld           vr2,   a1,    0
    vreplgr2vr.w  vr3,   t1
    vreplgr2vr.w  vr4,   t1
    vreplgr2vr.w  vr5,   t1
    vreplgr2vr.w  vr6,   t1
    vmin.h        vr1,   vr1,   vr7
    vmin.h        vr2,   vr2,   vr7
    vmaddwev.w.h  vr3,   vr0,   vr1
    vmaddwod.w.h  vr4,   vr0,   vr1
    vmaddwev.w.h  vr5,   vr0,   vr2
    vmaddwod.w.h  vr6,   vr0,   vr2
    vsrai.w       vr3,   vr3,   12
    vsrai.w       vr4,   vr4,   12
    vsrai.w       vr5,   vr5,   12
    vsrai.w       vr6,   vr6,   12
    vpackev.h     vr1,   vr4,   vr3
    vpackev.h     vr2,   vr6,   vr5
    vst           vr1,   a0,    0
    vst           vr2,   a1,    0
    addi.d        a0,    a0,    16
    addi.d        a1,    a1,    16
    addi.d        t2,    t2,    -1
    bnez          t2,    1b
2:
    beqz          t3,    4f
3:
    ld.h          t4,    a0,    0
    ld.h          t5,    a1,    0
    vreplgr2vr.h  vr1,   t4
    vreplgr2vr.h  vr2,   t5
    vmin.h        vr1,   vr1,   vr7
    vmin.h        vr2,   vr2,   vr7
    vpickve2gr.h  t4,    vr1,   0
    vpickve2gr.h  t5,    vr2,   0
    mul.w         t4,    t4,    t0
    mul.w         t5,    t5,    t0
    add.w         t4,    t4,    t1
    add.w         t5,    t5,    t1
    srai.w        t4,    t4,    12
    srai.w        t5,    t5,    12
    st.h          t4,    a0,    0
    st.h          t5,    a1,    0
    addi.d        a0,    a0,    2
    addi.d        a1,    a1,    2
    addi.d        t3,    t3,    -1
    bnez          t3,    3b
4:
endfunc

function chrRangeToJpeg_lasx
    li.w           t0,    4663
    li.w           t1,    -9289992
    li.w           t2,    30775
    xvreplgr2vr.h  xr0,   t0
    xvreplgr2vr.h  xr7,   t2
    srli.w         t2,    a2,    4
    andi           t3,    a2,    15
    beqz           t2,    2f
1:
    xvld           xr1,   a0,    0
    xvld           xr2,   a1,    0
    xvreplgr2vr.w  xr3,   t1
    xvreplgr2vr.w  xr4,   t1
    xvreplgr2vr.w  xr5,   t1
    xvreplgr2vr.w  xr6,   t1
    xvmin.h        xr1,   xr1,   xr7
    xvmin.h        xr2,   xr2,   xr7
    xvmaddwev.w.h  xr3,   xr0,   xr1
    xvmaddwod.w.h  xr4,   xr0,   xr1
    xvmaddwev.w.h  xr5,   xr0,   xr2
    xvmaddwod.w.h  xr6,   xr0,   xr2
    xvsrai.w       xr3,   xr3,   12
    xvsrai.w       xr4,   xr4,   12
    xvsrai.w       xr5,   xr5,   12
    xvsrai.w       xr6,   xr6,   12
    xvpackev.h     xr1,   xr4,   xr3
    xvpackev.h     xr2,   xr6,   xr5
    xvst           xr1,   a0,    0
    xvst           xr2,   a1,    0
    addi.d         a0,    a0,    32
    addi.d         a1,    a1,    32
    addi.d         t2,    t2,    -1
    bnez           t2,    1b
2:
    beqz          t3,    4f
3:
    ld.h          t4,    a0,    0
    ld.h          t5,    a1,    0
    vreplgr2vr.h  vr1,   t4
    vreplgr2vr.h  vr2,   t5
    vmin.h        vr1,   vr1,   vr7
    vmin.h        vr2,   vr2,   vr7
    vpickve2gr.h  t4,    vr1,   0
    vpickve2gr.h  t5,    vr2,   0
    mul.w         t4,    t4,    t0
    mul.w         t5,    t5,    t0
    add.w         t4,    t4,    t1
    add.w         t5,    t5,    t1
    srai.w        t4,    t4,    12
    srai.w        t5,    t5,    12
    st.h          t4,    a0,    0
    st.h          t5,    a1,    0
    addi.d        a0,    a0,    2
    addi.d        a1,    a1,    2
    addi.d        t3,    t3,    -1
    bnez          t3,    3b
4:
endfunc

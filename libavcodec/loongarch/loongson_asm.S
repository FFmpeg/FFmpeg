/*
 * Loongson asm helper.
 *
 * Copyright (c) 2022 Loongson Technology Corporation Limited
 * Contributed by Gu Xiwei(guxiwei-hf@loongson.cn)
 *                Shiyou Yin(yinshiyou-hf@loongson.cn)
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

/**
 * MAJOR version: Macro usage changes.
 * MINOR version: Add new functions, or bug fixes.
 * MICRO version: Comment changes or implementation changes.
 */
#define LML_VERSION_MAJOR 0
#define LML_VERSION_MINOR 2
#define LML_VERSION_MICRO 0

/*
 *============================================================================
 * macros for specific projetc, set them as needed.
 * Following LoongML macros for your reference.
 *============================================================================
 */
#define ASM_PREF
#define DEFAULT_ALIGN    5

.macro function name, align=DEFAULT_ALIGN
.macro endfunc
    jirl    $r0, $r1, 0x0
    .size ASM_PREF\name, . - ASM_PREF\name
    .purgem endfunc
.endm
.text ;
.align \align ;
.globl ASM_PREF\name ;
.type  ASM_PREF\name, @function ;
ASM_PREF\name: ;
.endm

/**
 *  Attention: If align is not zero, the macro will use
 *  t7 until the end of function
 */
.macro alloc_stack size, align=0
.if \align
    .macro clean_stack
        add.d   sp, sp, t7
    .endm
    addi.d  sp, sp, - \size
    andi.d  t7, sp, \align - 1
    sub.d   sp, sp, t7
    addi.d  t7, t7, \size
.else
    .macro clean_stack
        addi.d  sp, sp, \size
    .endm
    addi.d  sp, sp, - \size
.endif
.endm

.macro  const name, align=DEFAULT_ALIGN
    .macro endconst
    .size  \name, . - \name
    .purgem endconst
    .endm
.section .rodata
.align   \align
\name:
.endm

/*
 *============================================================================
 * LoongArch register alias
 *============================================================================
 */

#define a0 $a0
#define a1 $a1
#define a2 $a2
#define a3 $a3
#define a4 $a4
#define a5 $a5
#define a6 $a6
#define a7 $a7

#define t0 $t0
#define t1 $t1
#define t2 $t2
#define t3 $t3
#define t4 $t4
#define t5 $t5
#define t6 $t6
#define t7 $t7
#define t8 $t8

#define s0 $s0
#define s1 $s1
#define s2 $s2
#define s3 $s3
#define s4 $s4
#define s5 $s5
#define s6 $s6
#define s7 $s7
#define s8 $s8

#define zero $zero
#define sp   $sp
#define ra   $ra

#define f0  $f0
#define f1  $f1
#define f2  $f2
#define f3  $f3
#define f4  $f4
#define f5  $f5
#define f6  $f6
#define f7  $f7
#define f8  $f8
#define f9  $f9
#define f10 $f10
#define f11 $f11
#define f12 $f12
#define f13 $f13
#define f14 $f14
#define f15 $f15
#define f16 $f16
#define f17 $f17
#define f18 $f18
#define f19 $f19
#define f20 $f20
#define f21 $f21
#define f22 $f22
#define f23 $f23
#define f24 $f24
#define f25 $f25
#define f26 $f26
#define f27 $f27
#define f28 $f28
#define f29 $f29
#define f30 $f30
#define f31 $f31

#define vr0 $vr0
#define vr1 $vr1
#define vr2 $vr2
#define vr3 $vr3
#define vr4 $vr4
#define vr5 $vr5
#define vr6 $vr6
#define vr7 $vr7
#define vr8 $vr8
#define vr9 $vr9
#define vr10 $vr10
#define vr11 $vr11
#define vr12 $vr12
#define vr13 $vr13
#define vr14 $vr14
#define vr15 $vr15
#define vr16 $vr16
#define vr17 $vr17
#define vr18 $vr18
#define vr19 $vr19
#define vr20 $vr20
#define vr21 $vr21
#define vr22 $vr22
#define vr23 $vr23
#define vr24 $vr24
#define vr25 $vr25
#define vr26 $vr26
#define vr27 $vr27
#define vr28 $vr28
#define vr29 $vr29
#define vr30 $vr30
#define vr31 $vr31

#define xr0 $xr0
#define xr1 $xr1
#define xr2 $xr2
#define xr3 $xr3
#define xr4 $xr4
#define xr5 $xr5
#define xr6 $xr6
#define xr7 $xr7
#define xr8 $xr8
#define xr9 $xr9
#define xr10 $xr10
#define xr11 $xr11
#define xr12 $xr12
#define xr13 $xr13
#define xr14 $xr14
#define xr15 $xr15
#define xr16 $xr16
#define xr17 $xr17
#define xr18 $xr18
#define xr19 $xr19
#define xr20 $xr20
#define xr21 $xr21
#define xr22 $xr22
#define xr23 $xr23
#define xr24 $xr24
#define xr25 $xr25
#define xr26 $xr26
#define xr27 $xr27
#define xr28 $xr28
#define xr29 $xr29
#define xr30 $xr30
#define xr31 $xr31

/*
 *============================================================================
 * LSX/LASX synthesize instructions
 *============================================================================
 */

/*
 * Description : Dot product of byte vector elements
 * Arguments   : Inputs  - vj, vk
 *               Outputs - vd
 *               Return Type - halfword
 */
.macro vdp2.h.bu vd, vj, vk
    vmulwev.h.bu      \vd,    \vj,    \vk
    vmaddwod.h.bu     \vd,    \vj,    \vk
.endm

.macro vdp2.h.bu.b vd, vj, vk
    vmulwev.h.bu.b    \vd,    \vj,    \vk
    vmaddwod.h.bu.b   \vd,    \vj,    \vk
.endm

.macro vdp2.w.h vd, vj, vk
    vmulwev.w.h       \vd,    \vj,    \vk
    vmaddwod.w.h      \vd,    \vj,    \vk
.endm

.macro xvdp2.h.bu xd, xj, xk
    xvmulwev.h.bu    \xd,    \xj,    \xk
    xvmaddwod.h.bu   \xd,    \xj,    \xk
.endm

.macro xvdp2.h.bu.b xd, xj, xk
    xvmulwev.h.bu.b    \xd,  \xj,    \xk
    xvmaddwod.h.bu.b   \xd,  \xj,    \xk
.endm

.macro xvdp2.w.h xd, xj, xk
    xvmulwev.w.h       \xd,  \xj,    \xk
    xvmaddwod.w.h      \xd,  \xj,    \xk
.endm

/*
 * Description : Dot product & addition of halfword vector elements
 * Arguments   : Inputs  - vj, vk
 *               Outputs - vd
 *               Return Type - twice size of input
 */
.macro vdp2add.h.bu vd, vj, vk
    vmaddwev.h.bu     \vd,    \vj,    \vk
    vmaddwod.h.bu     \vd,    \vj,    \vk
.endm

.macro vdp2add.h.bu.b vd, vj, vk
    vmaddwev.h.bu.b   \vd,    \vj,    \vk
    vmaddwod.h.bu.b   \vd,    \vj,    \vk
.endm

.macro vdp2add.w.h vd, vj, vk
    vmaddwev.w.h      \vd,    \vj,    \vk
    vmaddwod.w.h      \vd,    \vj,    \vk
.endm

.macro xvdp2add.h.bu.b xd, xj, xk
    xvmaddwev.h.bu.b   \xd,  \xj,    \xk
    xvmaddwod.h.bu.b   \xd,  \xj,    \xk
.endm

.macro xvdp2add.w.h xd, xj, xk
    xvmaddwev.w.h      \xd,  \xj,    \xk
    xvmaddwod.w.h      \xd,  \xj,    \xk
.endm

/*
 * Description : Range each element of vector
 * clip: vj > vk ? vj : vk && vj < va ? vj : va
 * clip255: vj < 255 ? vj : 255 && vj > 0 ? vj : 0
 */
.macro vclip.h  vd,  vj, vk, va
    vmax.h    \vd,  \vj,   \vk
    vmin.h    \vd,  \vd,   \va
.endm

.macro vclip255.w  vd, vj
    vmaxi.w   \vd,   \vj,  0
    vsat.wu   \vd,   \vd,  7
.endm

.macro vclip255.h  vd, vj
    vmaxi.h   \vd,   \vj,  0
    vsat.hu   \vd,   \vd,  7
.endm

.macro xvclip.h  xd,  xj, xk, xa
    xvmax.h    \xd,  \xj,   \xk
    xvmin.h    \xd,  \xd,   \xa
.endm

.macro xvclip255.h  xd, xj
    xvmaxi.h   \xd,   \xj,  0
    xvsat.hu   \xd,   \xd,  7
.endm

.macro xvclip255.w  xd, xj
    xvmaxi.w   \xd,   \xj,  0
    xvsat.wu   \xd,   \xd,  7
.endm

/*
 * Description : Store elements of vector
 * vd : Data vector to be stroed
 * rk : Address of data storage
 * ra : Offset of address
 * si : Index of data in vd
 */
.macro vstelmx.b vd, rk, ra, si
    add.d      \rk,  \rk,  \ra
    vstelm.b   \vd,  \rk,  0, \si
.endm

.macro vstelmx.h vd, rk, ra, si
    add.d      \rk,  \rk,  \ra
    vstelm.h   \vd,  \rk,  0, \si
.endm

.macro vstelmx.w vd, rk, ra, si
    add.d      \rk,  \rk,  \ra
    vstelm.w   \vd,  \rk,  0, \si
.endm

.macro vstelmx.d  vd, rk, ra, si
    add.d      \rk,  \rk,  \ra
    vstelm.d   \vd,  \rk,  0, \si
.endm

.macro vmov xd, xj
    vor.v  \xd,  \xj,  \xj
.endm

.macro xmov xd, xj
    xvor.v  \xd,  \xj,  \xj
.endm

.macro xvstelmx.d  xd, rk, ra, si
    add.d      \rk, \rk,  \ra
    xvstelm.d  \xd, \rk,  0, \si
.endm

/*
 *============================================================================
 * LSX/LASX custom macros
 *============================================================================
 */

/*
 * Load 4 float, double, V128, v256 elements with stride.
 */
.macro FLDS_LOADX_4 src, stride, stride2, stride3, out0, out1, out2, out3
    fld.s     \out0,    \src,    0
    fldx.s    \out1,    \src,    \stride
    fldx.s    \out2,    \src,    \stride2
    fldx.s    \out3,    \src,    \stride3
.endm

.macro FLDD_LOADX_4 src, stride, stride2, stride3, out0, out1, out2, out3
    fld.d     \out0,    \src,    0
    fldx.d    \out1,    \src,    \stride
    fldx.d    \out2,    \src,    \stride2
    fldx.d    \out3,    \src,    \stride3
.endm

.macro LSX_LOADX_4 src, stride, stride2, stride3, out0, out1, out2, out3
    vld     \out0,    \src,    0
    vldx    \out1,    \src,    \stride
    vldx    \out2,    \src,    \stride2
    vldx    \out3,    \src,    \stride3
.endm

.macro LASX_LOADX_4 src, stride, stride2, stride3, out0, out1, out2, out3
    xvld    \out0,    \src,    0
    xvldx   \out1,    \src,    \stride
    xvldx   \out2,    \src,    \stride2
    xvldx   \out3,    \src,    \stride3
.endm

/*
 * Description : Transpose 4x4 block with half-word elements in vectors
 * Arguments   : Inputs  - in0, in1, in2, in3
 *               Outputs - out0, out1, out2, out3
 */
.macro LSX_TRANSPOSE4x4_H in0, in1, in2, in3, out0, out1, out2, out3, \
                          tmp0, tmp1
    vilvl.h   \tmp0,  \in1,   \in0
    vilvl.h   \tmp1,  \in3,   \in2
    vilvl.w   \out0,  \tmp1,  \tmp0
    vilvh.w   \out2,  \tmp1,  \tmp0
    vilvh.d   \out1,  \out0,  \out0
    vilvh.d   \out3,  \out0,  \out2
.endm

/*
 * Description : Transpose 4x4 block with word elements in vectors
 * Arguments   : Inputs  - in0, in1, in2, in3
 *               Outputs - out0, out1, out2, out3
 * Details     :
 * Example     :
 *               1, 2, 3, 4            1, 5, 9,13
 *               5, 6, 7, 8    to      2, 6,10,14
 *               9,10,11,12  =====>    3, 7,11,15
 *              13,14,15,16            4, 8,12,16
 */
.macro LSX_TRANSPOSE4x4_W _in0, _in1, _in2, _in3, _out0, _out1, _out2, _out3, \
                          _tmp0, _tmp1

    vilvl.w    \_tmp0,   \_in1,    \_in0
    vilvh.w    \_out1,   \_in1,    \_in0
    vilvl.w    \_tmp1,   \_in3,    \_in2
    vilvh.w    \_out3,   \_in3,    \_in2

    vilvl.d    \_out0,   \_tmp1,   \_tmp0
    vilvl.d    \_out2,   \_out3,   \_out1
    vilvh.d    \_out3,   \_out3,   \_out1
    vilvh.d    \_out1,   \_tmp1,   \_tmp0
.endm

/*
 * Description : Transpose 8x8 block with half-word elements in vectors
 * Arguments   : Inputs  - in0, in1, in2, in3, in4, in5, in6, in7
 *               Outputs - out0, out1, out2, out3, out4, out5, out6, out7
 */
.macro LSX_TRANSPOSE8x8_H in0, in1, in2, in3, in4, in5, in6, in7, out0, out1,   \
                          out2, out3, out4, out5, out6, out7, tmp0, tmp1, tmp2, \
                          tmp3, tmp4, tmp5, tmp6, tmp7
    vilvl.h      \tmp0,    \in6,   \in4
    vilvl.h      \tmp1,    \in7,   \in5
    vilvl.h      \tmp2,    \in2,   \in0
    vilvl.h      \tmp3,    \in3,   \in1

    vilvl.h      \tmp4,    \tmp1,  \tmp0
    vilvh.h      \tmp5,    \tmp1,  \tmp0
    vilvl.h      \tmp6,    \tmp3,  \tmp2
    vilvh.h      \tmp7,    \tmp3,  \tmp2

    vilvh.h      \tmp0,    \in6,   \in4
    vilvh.h      \tmp1,    \in7,   \in5
    vilvh.h      \tmp2,    \in2,   \in0
    vilvh.h      \tmp3,    \in3,   \in1

    vpickev.d    \out0,    \tmp4,  \tmp6
    vpickod.d    \out1,    \tmp4,  \tmp6
    vpickev.d    \out2,    \tmp5,  \tmp7
    vpickod.d    \out3,    \tmp5,  \tmp7

    vilvl.h      \tmp4,    \tmp1,  \tmp0
    vilvh.h      \tmp5,    \tmp1,  \tmp0
    vilvl.h      \tmp6,    \tmp3,  \tmp2
    vilvh.h      \tmp7,    \tmp3,  \tmp2

    vpickev.d    \out4,    \tmp4,  \tmp6
    vpickod.d    \out5,    \tmp4,  \tmp6
    vpickev.d    \out6,    \tmp5,  \tmp7
    vpickod.d    \out7,    \tmp5,  \tmp7
.endm

/*
 * Description : Transpose 16x8 block with byte elements in vectors
 * Arguments   : Inputs  - in0, in1, in2, in3, in4, in5, in6, in7
 *               Outputs - out0, out1, out2, out3, out4, out5, out6, out7
 */
.macro LASX_TRANSPOSE16X8_B in0, in1, in2, in3, in4, in5, in6, in7,        \
                            in8, in9, in10, in11, in12, in13, in14, in15,  \
                            out0, out1, out2, out3, out4, out5, out6, out7,\
                            tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7
    xvilvl.b   \tmp0,    \in2,     \in0
    xvilvl.b   \tmp1,    \in3,     \in1
    xvilvl.b   \tmp2,    \in6,     \in4
    xvilvl.b   \tmp3,    \in7,     \in5
    xvilvl.b   \tmp4,    \in10,    \in8
    xvilvl.b   \tmp5,    \in11,    \in9
    xvilvl.b   \tmp6,    \in14,    \in12
    xvilvl.b   \tmp7,    \in15,    \in13
    xvilvl.b   \out0,    \tmp1,    \tmp0
    xvilvh.b   \out1,    \tmp1,    \tmp0
    xvilvl.b   \out2,    \tmp3,    \tmp2
    xvilvh.b   \out3,    \tmp3,    \tmp2
    xvilvl.b   \out4,    \tmp5,    \tmp4
    xvilvh.b   \out5,    \tmp5,    \tmp4
    xvilvl.b   \out6,    \tmp7,    \tmp6
    xvilvh.b   \out7,    \tmp7,    \tmp6
    xvilvl.w   \tmp0,    \out2,    \out0
    xvilvh.w   \tmp2,    \out2,    \out0
    xvilvl.w   \tmp4,    \out3,    \out1
    xvilvh.w   \tmp6,    \out3,    \out1
    xvilvl.w   \tmp1,    \out6,    \out4
    xvilvh.w   \tmp3,    \out6,    \out4
    xvilvl.w   \tmp5,    \out7,    \out5
    xvilvh.w   \tmp7,    \out7,    \out5
    xvilvl.d   \out0,    \tmp1,    \tmp0
    xvilvh.d   \out1,    \tmp1,    \tmp0
    xvilvl.d   \out2,    \tmp3,    \tmp2
    xvilvh.d   \out3,    \tmp3,    \tmp2
    xvilvl.d   \out4,    \tmp5,    \tmp4
    xvilvh.d   \out5,    \tmp5,    \tmp4
    xvilvl.d   \out6,    \tmp7,    \tmp6
    xvilvh.d   \out7,    \tmp7,    \tmp6
.endm

/*
 * Description : Transpose 16x8 block with byte elements in vectors
 * Arguments   : Inputs  - in0, in1, in2, in3, in4, in5, in6, in7
 *               Outputs - out0, out1, out2, out3, out4, out5, out6, out7
 */
.macro LSX_TRANSPOSE16X8_B in0, in1, in2, in3, in4, in5, in6, in7,        \
                           in8, in9, in10, in11, in12, in13, in14, in15,  \
                           out0, out1, out2, out3, out4, out5, out6, out7,\
                           tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7
    vilvl.b   \tmp0,    \in2,     \in0
    vilvl.b   \tmp1,    \in3,     \in1
    vilvl.b   \tmp2,    \in6,     \in4
    vilvl.b   \tmp3,    \in7,     \in5
    vilvl.b   \tmp4,    \in10,    \in8
    vilvl.b   \tmp5,    \in11,    \in9
    vilvl.b   \tmp6,    \in14,    \in12
    vilvl.b   \tmp7,    \in15,    \in13

    vilvl.b   \out0,    \tmp1,    \tmp0
    vilvh.b   \out1,    \tmp1,    \tmp0
    vilvl.b   \out2,    \tmp3,    \tmp2
    vilvh.b   \out3,    \tmp3,    \tmp2
    vilvl.b   \out4,    \tmp5,    \tmp4
    vilvh.b   \out5,    \tmp5,    \tmp4
    vilvl.b   \out6,    \tmp7,    \tmp6
    vilvh.b   \out7,    \tmp7,    \tmp6
    vilvl.w   \tmp0,    \out2,    \out0
    vilvh.w   \tmp2,    \out2,    \out0
    vilvl.w   \tmp4,    \out3,    \out1
    vilvh.w   \tmp6,    \out3,    \out1
    vilvl.w   \tmp1,    \out6,    \out4
    vilvh.w   \tmp3,    \out6,    \out4
    vilvl.w   \tmp5,    \out7,    \out5
    vilvh.w   \tmp7,    \out7,    \out5
    vilvl.d   \out0,    \tmp1,    \tmp0
    vilvh.d   \out1,    \tmp1,    \tmp0
    vilvl.d   \out2,    \tmp3,    \tmp2
    vilvh.d   \out3,    \tmp3,    \tmp2
    vilvl.d   \out4,    \tmp5,    \tmp4
    vilvh.d   \out5,    \tmp5,    \tmp4
    vilvl.d   \out6,    \tmp7,    \tmp6
    vilvh.d   \out7,    \tmp7,    \tmp6
.endm

/*
 * Description : Transpose 4x4 block with half-word elements in vectors
 * Arguments   : Inputs  - in0, in1, in2, in3
 *               Outputs - out0, out1, out2, out3
 */
.macro LASX_TRANSPOSE4x4_H in0, in1, in2, in3, out0, out1, out2, out3, \
                           tmp0, tmp1
    xvilvl.h   \tmp0,  \in1,   \in0
    xvilvl.h   \tmp1,  \in3,   \in2
    xvilvl.w   \out0,  \tmp1,  \tmp0
    xvilvh.w   \out2,  \tmp1,  \tmp0
    xvilvh.d   \out1,  \out0,  \out0
    xvilvh.d   \out3,  \out0,  \out2
.endm

/*
 * Description : Transpose 4x8 block with half-word elements in vectors
 * Arguments   : Inputs  - in0, in1, in2, in3
 *               Outputs - out0, out1, out2, out3
 */
.macro LASX_TRANSPOSE4x8_H in0, in1, in2, in3, out0, out1, out2, out3, \
                           tmp0, tmp1
    xvilvl.h      \tmp0,    \in2,   \in0
    xvilvl.h      \tmp1,    \in3,   \in1
    xvilvl.h      \out2,    \tmp1,  \tmp0
    xvilvh.h      \out3,    \tmp1,  \tmp0

    xvilvl.d      \out0,    \out2,  \out2
    xvilvh.d      \out1,    \out2,  \out2
    xvilvl.d      \out2,    \out3,  \out3
    xvilvh.d      \out3,    \out3,  \out3
.endm

/*
 * Description : Transpose 8x8 block with half-word elements in vectors
 * Arguments   : Inputs  - in0, in1, in2, in3, in4, in5, in6, in7
 *               Outputs - out0, out1, out2, out3, out4, out5, out6, out7
 */
.macro LASX_TRANSPOSE8x8_H in0, in1, in2, in3, in4, in5, in6, in7,         \
                           out0, out1, out2, out3, out4, out5, out6, out7, \
                           tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7
    xvilvl.h     \tmp0,   \in6,     \in4
    xvilvl.h     \tmp1,   \in7,     \in5
    xvilvl.h     \tmp2,   \in2,     \in0
    xvilvl.h     \tmp3,   \in3,     \in1

    xvilvl.h     \tmp4,   \tmp1,    \tmp0
    xvilvh.h     \tmp5,   \tmp1,    \tmp0
    xvilvl.h     \tmp6,   \tmp3,    \tmp2
    xvilvh.h     \tmp7,   \tmp3,    \tmp2

    xvilvh.h     \tmp0,   \in6,     \in4
    xvilvh.h     \tmp1,   \in7,     \in5
    xvilvh.h     \tmp2,   \in2,     \in0
    xvilvh.h     \tmp3,   \in3,     \in1

    xvpickev.d   \out0,   \tmp4,    \tmp6
    xvpickod.d   \out1,   \tmp4,    \tmp6
    xvpickev.d   \out2,   \tmp5,    \tmp7
    xvpickod.d   \out3,   \tmp5,    \tmp7

    xvilvl.h     \tmp4,   \tmp1,    \tmp0
    xvilvh.h     \tmp5,   \tmp1,    \tmp0
    xvilvl.h     \tmp6,   \tmp3,    \tmp2
    xvilvh.h     \tmp7,   \tmp3,    \tmp2

    xvpickev.d   \out4,   \tmp4,    \tmp6
    xvpickod.d   \out5,   \tmp4,    \tmp6
    xvpickev.d   \out6,   \tmp5,    \tmp7
    xvpickod.d   \out7,   \tmp5,    \tmp7
.endm

/*
 * Description : Transpose 2x4x4 block with half-word elements in vectors
 * Arguments   : Inputs  - in0, in1, in2, in3
 *               Outputs - out0, out1, out2, out3
 */
.macro LASX_TRANSPOSE2x4x4_H in0, in1, in2, in3, out0, out1, out2, out3, \
                             tmp0, tmp1, tmp2
    xvilvh.h   \tmp1,    \in0,     \in1
    xvilvl.h   \out1,    \in0,     \in1
    xvilvh.h   \tmp0,    \in2,     \in3
    xvilvl.h   \out3,    \in2,     \in3

    xvilvh.w   \tmp2,    \out3,    \out1
    xvilvl.w   \out3,    \out3,    \out1

    xvilvl.w   \out2,    \tmp0,    \tmp1
    xvilvh.w   \tmp1,    \tmp0,    \tmp1

    xvilvh.d   \out0,    \out2,    \out3
    xvilvl.d   \out2,    \out2,    \out3
    xvilvh.d   \out1,    \tmp1,    \tmp2
    xvilvl.d   \out3,    \tmp1,    \tmp2
.endm

/*
 * Description : Transpose 4x4 block with word elements in vectors
 * Arguments   : Inputs  - in0, in1, in2, in3
 *               Outputs - out0, out1, out2, out3
 * Details     :
 * Example     :
 *               1, 2, 3, 4,  1, 2, 3, 4        1,5, 9,13, 1,5, 9,13
 *               5, 6, 7, 8,  5, 6, 7, 8   to   2,6,10,14, 2,6,10,14
 *               9,10,11,12,  9,10,11,12 =====> 3,7,11,15, 3,7,11,15
 *              13,14,15,16, 13,14,15,16        4,8,12,16, 4,8,12,16
 */
.macro LASX_TRANSPOSE4x4_W _in0, _in1, _in2, _in3, _out0, _out1, _out2, _out3, \
                           _tmp0, _tmp1

    xvilvl.w    \_tmp0,   \_in1,    \_in0
    xvilvh.w    \_out1,   \_in1,    \_in0
    xvilvl.w    \_tmp1,   \_in3,    \_in2
    xvilvh.w    \_out3,   \_in3,    \_in2

    xvilvl.d    \_out0,   \_tmp1,   \_tmp0
    xvilvl.d    \_out2,   \_out3,   \_out1
    xvilvh.d    \_out3,   \_out3,   \_out1
    xvilvh.d    \_out1,   \_tmp1,   \_tmp0
.endm

/*
 * Description : Transpose 8x8 block with word elements in vectors
 * Arguments   : Inputs  - _in0, _in1, _in2, _in3, _in4, _in5, _in6, _in7
 *               Outputs - _out0, _out1, _out2, _out3, _out4, _out5, _out6,
 *               _out7
 * Example     : LASX_TRANSPOSE8x8_W
 *        _in0 : 1,2,3,4,5,6,7,8
 *        _in1 : 2,2,3,4,5,6,7,8
 *        _in2 : 3,2,3,4,5,6,7,8
 *        _in3 : 4,2,3,4,5,6,7,8
 *        _in4 : 5,2,3,4,5,6,7,8
 *        _in5 : 6,2,3,4,5,6,7,8
 *        _in6 : 7,2,3,4,5,6,7,8
 *        _in7 : 8,2,3,4,5,6,7,8
 *
 *       _out0 : 1,2,3,4,5,6,7,8
 *       _out1 : 2,2,2,2,2,2,2,2
 *       _out2 : 3,3,3,3,3,3,3,3
 *       _out3 : 4,4,4,4,4,4,4,4
 *       _out4 : 5,5,5,5,5,5,5,5
 *       _out5 : 6,6,6,6,6,6,6,6
 *       _out6 : 7,7,7,7,7,7,7,7
 *       _out7 : 8,8,8,8,8,8,8,8
 */
.macro LASX_TRANSPOSE8x8_W _in0, _in1, _in2, _in3, _in4, _in5, _in6, _in7,\
                           _out0, _out1, _out2, _out3, _out4, _out5, _out6, _out7,\
                           _tmp0, _tmp1, _tmp2, _tmp3
    xvilvl.w    \_tmp0,   \_in2,    \_in0
    xvilvl.w    \_tmp1,   \_in3,    \_in1
    xvilvh.w    \_tmp2,   \_in2,    \_in0
    xvilvh.w    \_tmp3,   \_in3,    \_in1
    xvilvl.w    \_out0,   \_tmp1,   \_tmp0
    xvilvh.w    \_out1,   \_tmp1,   \_tmp0
    xvilvl.w    \_out2,   \_tmp3,   \_tmp2
    xvilvh.w    \_out3,   \_tmp3,   \_tmp2

    xvilvl.w    \_tmp0,   \_in6,    \_in4
    xvilvl.w    \_tmp1,   \_in7,    \_in5
    xvilvh.w    \_tmp2,   \_in6,    \_in4
    xvilvh.w    \_tmp3,   \_in7,    \_in5
    xvilvl.w    \_out4,   \_tmp1,   \_tmp0
    xvilvh.w    \_out5,   \_tmp1,   \_tmp0
    xvilvl.w    \_out6,   \_tmp3,   \_tmp2
    xvilvh.w    \_out7,   \_tmp3,   \_tmp2

    xmov        \_tmp0,   \_out0
    xmov        \_tmp1,   \_out1
    xmov        \_tmp2,   \_out2
    xmov        \_tmp3,   \_out3
    xvpermi.q   \_out0,   \_out4,   0x02
    xvpermi.q   \_out1,   \_out5,   0x02
    xvpermi.q   \_out2,   \_out6,   0x02
    xvpermi.q   \_out3,   \_out7,   0x02
    xvpermi.q   \_out4,   \_tmp0,   0x31
    xvpermi.q   \_out5,   \_tmp1,   0x31
    xvpermi.q   \_out6,   \_tmp2,   0x31
    xvpermi.q   \_out7,   \_tmp3,   0x31
.endm

/*
 * Description : Transpose 4x4 block with double-word elements in vectors
 * Arguments   : Inputs  - _in0, _in1, _in2, _in3
 *               Outputs - _out0, _out1, _out2, _out3
 * Example     : LASX_TRANSPOSE4x4_D
 *        _in0 : 1,2,3,4
 *        _in1 : 1,2,3,4
 *        _in2 : 1,2,3,4
 *        _in3 : 1,2,3,4
 *
 *       _out0 : 1,1,1,1
 *       _out1 : 2,2,2,2
 *       _out2 : 3,3,3,3
 *       _out3 : 4,4,4,4
 */
.macro LASX_TRANSPOSE4x4_D _in0, _in1, _in2, _in3, _out0, _out1, _out2, _out3, \
                           _tmp0, _tmp1
    xvilvl.d    \_tmp0,   \_in1,    \_in0
    xvilvh.d    \_out1,   \_in1,    \_in0
    xvilvh.d    \_tmp1,   \_in3,    \_in2
    xvilvl.d    \_out2,   \_in3,    \_in2

    xvor.v      \_out0,   \_tmp0,   \_tmp0
    xvor.v      \_out3,   \_tmp1,   \_tmp1

    xvpermi.q   \_out0,   \_out2,   0x02
    xvpermi.q   \_out2,   \_tmp0,   0x31
    xvpermi.q   \_out3,   \_out1,   0x31
    xvpermi.q   \_out1,   \_tmp1,   0x02
.endm

/*
 * Description : Butterfly of 4 input vectors
 * Arguments   : Inputs  - _in0, _in1, _in2, _in3
 *               Outputs - _out0, _out1, _out2, _out3
 * Details     : Butterfly operation
 * Example     : LSX_BUTTERFLY_4
 *               _out0 = _in0 + _in3;
 *               _out1 = _in1 + _in2;
 *               _out2 = _in1 - _in2;
 *               _out3 = _in0 - _in3;
 */
.macro LSX_BUTTERFLY_4_B _in0, _in1, _in2, _in3, _out0, _out1, _out2, _out3
    vadd.b   \_out0,   \_in0,   \_in3
    vadd.b   \_out1,   \_in1,   \_in2
    vsub.b   \_out2,   \_in1,   \_in2
    vsub.b   \_out3,   \_in0,   \_in3
.endm
.macro LSX_BUTTERFLY_4_H _in0, _in1, _in2, _in3, _out0, _out1, _out2, _out3
    vadd.h   \_out0,   \_in0,   \_in3
    vadd.h   \_out1,   \_in1,   \_in2
    vsub.h   \_out2,   \_in1,   \_in2
    vsub.h   \_out3,   \_in0,   \_in3
.endm
.macro LSX_BUTTERFLY_4_W _in0, _in1, _in2, _in3, _out0, _out1, _out2, _out3
    vadd.w   \_out0,   \_in0,   \_in3
    vadd.w   \_out1,   \_in1,   \_in2
    vsub.w   \_out2,   \_in1,   \_in2
    vsub.w   \_out3,   \_in0,   \_in3
.endm
.macro LSX_BUTTERFLY_4_D _in0, _in1, _in2, _in3, _out0, _out1, _out2, _out3
    vadd.d   \_out0,   \_in0,   \_in3
    vadd.d   \_out1,   \_in1,   \_in2
    vsub.d   \_out2,   \_in1,   \_in2
    vsub.d   \_out3,   \_in0,   \_in3
.endm

.macro LASX_BUTTERFLY_4_B _in0, _in1, _in2, _in3, _out0, _out1, _out2, _out3
    xvadd.b   \_out0,   \_in0,   \_in3
    xvadd.b   \_out1,   \_in1,   \_in2
    xvsub.b   \_out2,   \_in1,   \_in2
    xvsub.b   \_out3,   \_in0,   \_in3
.endm
.macro LASX_BUTTERFLY_4_H _in0, _in1, _in2, _in3, _out0, _out1, _out2, _out3
    xvadd.h   \_out0,   \_in0,   \_in3
    xvadd.h   \_out1,   \_in1,   \_in2
    xvsub.h   \_out2,   \_in1,   \_in2
    xvsub.h   \_out3,   \_in0,   \_in3
.endm
.macro LASX_BUTTERFLY_4_W _in0, _in1, _in2, _in3, _out0, _out1, _out2, _out3
    xvadd.w   \_out0,   \_in0,   \_in3
    xvadd.w   \_out1,   \_in1,   \_in2
    xvsub.w   \_out2,   \_in1,   \_in2
    xvsub.w   \_out3,   \_in0,   \_in3
.endm
.macro LASX_BUTTERFLY_4_D _in0, _in1, _in2, _in3, _out0, _out1, _out2, _out3
    xvadd.d   \_out0,   \_in0,   \_in3
    xvadd.d   \_out1,   \_in1,   \_in2
    xvsub.d   \_out2,   \_in1,   \_in2
    xvsub.d   \_out3,   \_in0,   \_in3
.endm

/*
 * Description : Butterfly of 8 input vectors
 * Arguments   : Inputs  - _in0, _in1, _in2, _in3, ~
 *               Outputs - _out0, _out1, _out2, _out3, ~
 * Details     : Butterfly operation
 * Example     : LASX_BUTTERFLY_8
 *               _out0 = _in0 + _in7;
 *               _out1 = _in1 + _in6;
 *               _out2 = _in2 + _in5;
 *               _out3 = _in3 + _in4;
 *               _out4 = _in3 - _in4;
 *               _out5 = _in2 - _in5;
 *               _out6 = _in1 - _in6;
 *               _out7 = _in0 - _in7;
 */
.macro LSX_BUTTERFLY_8_B _in0,  _in1,  _in2,  _in3,  _in4,  _in5,  _in6,  _in7, \
                         _out0, _out1, _out2, _out3, _out4, _out5, _out6, _out7
    vadd.b    \_out0,    \_in0,    \_in7
    vadd.b    \_out1,    \_in1,    \_in6
    vadd.b    \_out2,    \_in2,    \_in5
    vadd.b    \_out3,    \_in3,    \_in4
    vsub.b    \_out4,    \_in3,    \_in4
    vsub.b    \_out5,    \_in2,    \_in5
    vsub.b    \_out6,    \_in1,    \_in6
    vsub.b    \_out7,    \_in0,    \_in7
.endm

.macro LSX_BUTTERFLY_8_H _in0,  _in1,  _in2,  _in3,  _in4,  _in5,  _in6,  _in7, \
                         _out0, _out1, _out2, _out3, _out4, _out5, _out6, _out7
    vadd.h    \_out0,    \_in0,    \_in7
    vadd.h    \_out1,    \_in1,    \_in6
    vadd.h    \_out2,    \_in2,    \_in5
    vadd.h    \_out3,    \_in3,    \_in4
    vsub.h    \_out4,    \_in3,    \_in4
    vsub.h    \_out5,    \_in2,    \_in5
    vsub.h    \_out6,    \_in1,    \_in6
    vsub.h    \_out7,    \_in0,    \_in7
.endm

.macro LSX_BUTTERFLY_8_W _in0,  _in1,  _in2,  _in3,  _in4,  _in5,  _in6,  _in7, \
                         _out0, _out1, _out2, _out3, _out4, _out5, _out6, _out7
    vadd.w    \_out0,    \_in0,    \_in7
    vadd.w    \_out1,    \_in1,    \_in6
    vadd.w    \_out2,    \_in2,    \_in5
    vadd.w    \_out3,    \_in3,    \_in4
    vsub.w    \_out4,    \_in3,    \_in4
    vsub.w    \_out5,    \_in2,    \_in5
    vsub.w    \_out6,    \_in1,    \_in6
    vsub.w    \_out7,    \_in0,    \_in7
.endm

.macro LSX_BUTTERFLY_8_D _in0,  _in1,  _in2,  _in3,  _in4,  _in5,  _in6,  _in7, \
                         _out0, _out1, _out2, _out3, _out4, _out5, _out6, _out7
    vadd.d    \_out0,    \_in0,    \_in7
    vadd.d    \_out1,    \_in1,    \_in6
    vadd.d    \_out2,    \_in2,    \_in5
    vadd.d    \_out3,    \_in3,    \_in4
    vsub.d    \_out4,    \_in3,    \_in4
    vsub.d    \_out5,    \_in2,    \_in5
    vsub.d    \_out6,    \_in1,    \_in6
    vsub.d    \_out7,    \_in0,    \_in7
.endm

.macro LASX_BUTTERFLY_8_B _in0,  _in1,  _in2,  _in3,  _in4,  _in5,  _in6,  _in7, \
                          _out0, _out1, _out2, _out3, _out4, _out5, _out6, _out7
    xvadd.b    \_out0,    \_in0,    \_in7
    xvadd.b    \_out1,    \_in1,    \_in6
    xvadd.b    \_out2,    \_in2,    \_in5
    xvadd.b    \_out3,    \_in3,    \_in4
    xvsub.b    \_out4,    \_in3,    \_in4
    xvsub.b    \_out5,    \_in2,    \_in5
    xvsub.b    \_out6,    \_in1,    \_in6
    xvsub.b    \_out7,    \_in0,    \_in7
.endm

.macro LASX_BUTTERFLY_8_H _in0,  _in1,  _in2,  _in3,  _in4,  _in5,  _in6,  _in7, \
                          _out0, _out1, _out2, _out3, _out4, _out5, _out6, _out7
    xvadd.h    \_out0,    \_in0,    \_in7
    xvadd.h    \_out1,    \_in1,    \_in6
    xvadd.h    \_out2,    \_in2,    \_in5
    xvadd.h    \_out3,    \_in3,    \_in4
    xvsub.h    \_out4,    \_in3,    \_in4
    xvsub.h    \_out5,    \_in2,    \_in5
    xvsub.h    \_out6,    \_in1,    \_in6
    xvsub.h    \_out7,    \_in0,    \_in7
.endm

.macro LASX_BUTTERFLY_8_W _in0,  _in1,  _in2,  _in3,  _in4,  _in5,  _in6,  _in7, \
                          _out0, _out1, _out2, _out3, _out4, _out5, _out6, _out7
    xvadd.w    \_out0,    \_in0,    \_in7
    xvadd.w    \_out1,    \_in1,    \_in6
    xvadd.w    \_out2,    \_in2,    \_in5
    xvadd.w    \_out3,    \_in3,    \_in4
    xvsub.w    \_out4,    \_in3,    \_in4
    xvsub.w    \_out5,    \_in2,    \_in5
    xvsub.w    \_out6,    \_in1,    \_in6
    xvsub.w    \_out7,    \_in0,    \_in7
.endm

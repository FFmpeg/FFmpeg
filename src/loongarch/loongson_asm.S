/*********************************************************************
 * Copyright (c) 2022 Loongson Technology Corporation Limited
 * Contributed by Gu Xiwei(guxiwei-hf@loongson.cn)
 *                Shiyou Yin(yinshiyou-hf@loongson.cn)
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *********************************************************************/

/*
 * This file is a LoongArch assembly helper file and available under ISC
 * license. It provides a large number of macros and alias to simplify
 * writing assembly code, especially for LSX and LASX optimizations.
 *
 * Any one can modify it or add new features for his/her own purposes.
 * Contributing a patch will be appreciated as it might be useful for
 * others as well. Send patches to loongson contributor mentioned above.
 *
 * MAJOR version: Usage changes, incompatible with previous version.
 * MINOR version: Add new macros/functions, or bug fixes.
 * MICRO version: Comment changes or implementation changes.
 */

#define LML_VERSION_MAJOR 0
#define LML_VERSION_MINOR 4
#define LML_VERSION_MICRO 0

#define DEFAULT_ALIGN    5

/* Set prefix as needed. */
#ifndef PRIVATE_PREFIX
#define PRIVATE_PREFIX checkasm_
#endif

#define PASTE(a,b) a ## b
#define CONCAT(a,b) PASTE(a,b)

#ifdef PREFIX
#define ASM_PREF CONCAT(_,PRIVATE_PREFIX)
#else
#define ASM_PREF PRIVATE_PREFIX
#endif

.macro function name, align=DEFAULT_ALIGN, visible=0
.macro endfunc
    jirl    $r0, $r1, 0x0
    .size ASM_PREF\name, . - ASM_PREF\name
    .purgem endfunc
.endm
.text ;
.align \align ;
.globl ASM_PREF\name ;
.if !\visible
.hidden ASM_PREF\name ;
.endif
.type  ASM_PREF\name, @function ;
ASM_PREF\name: ;
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

#define fa0  $fa0
#define fa1  $fa1
#define fa2  $fa2
#define fa3  $fa3
#define fa4  $fa4
#define fa5  $fa5
#define fa6  $fa6
#define fa7  $fa7
#define ft0  $ft0
#define ft1  $ft1
#define ft2  $ft2
#define ft3  $ft3
#define ft4  $ft4
#define ft5  $ft5
#define ft6  $ft6
#define ft7  $ft7
#define ft8  $ft8
#define ft9  $ft9
#define ft10 $ft10
#define ft11 $ft11
#define ft12 $ft12
#define ft13 $ft13
#define ft14 $ft14
#define ft15 $ft15
#define fs0  $fs0
#define fs1  $fs1
#define fs2  $fs2
#define fs3  $fs3
#define fs4  $fs4
#define fs5  $fs5
#define fs6  $fs6
#define fs7  $fs7

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
 * Description : Range element vj[i] to vk[i] ~ vj[i]
 * clip: vj > vk ? vj : vk && vj < va ? vj : va
 */
.macro vclip.h  vd,  vj, vk, va
    vmax.h    \vd,  \vj,   \vk
    vmin.h    \vd,  \vd,   \va
.endm

.macro vclip.w  vd,  vj, vk, va
    vmax.w    \vd,  \vj,   \vk
    vmin.w    \vd,  \vd,   \va
.endm

.macro xvclip.h  xd,  xj, xk, xa
    xvmax.h    \xd,  \xj,   \xk
    xvmin.h    \xd,  \xd,   \xa
.endm

.macro xvclip.w  xd,  xj, xk, xa
    xvmax.w    \xd,  \xj,   \xk
    xvmin.w    \xd,  \xd,   \xa
.endm

/*
 * Description : Range element vj[i] to 0 ~ 255
 * clip255: vj < 255 ? vj : 255 && vj > 0 ? vj : 0
 */
.macro vclip255.h  vd, vj
    vmaxi.h   \vd,   \vj,  0
    vsat.hu   \vd,   \vd,  7
.endm

.macro vclip255.w  vd, vj
    vmaxi.w   \vd,   \vj,  0
    vsat.wu   \vd,   \vd,  7
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
 * vd : Data vector to be stored
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

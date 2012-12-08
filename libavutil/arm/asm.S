/*
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
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

#include "config.h"

#ifdef __ELF__
#   define ELF
#else
#   define ELF @
#endif

#if CONFIG_THUMB
#   define A @
#   define T
#else
#   define A
#   define T @
#endif

#if   HAVE_NEON
        .arch           armv7-a
#elif HAVE_ARMV6T2
        .arch           armv6t2
#elif HAVE_ARMV6
        .arch           armv6
#elif HAVE_ARMV5TE
        .arch           armv5te
#endif

#if   HAVE_NEON
        .fpu            neon
#elif HAVE_VFP
        .fpu            vfp
#endif

        .syntax unified
T       .thumb
ELF     .eabi_attribute 25, 1           @ Tag_ABI_align_preserved

.macro  function name, export=0
        .set            .Lpic_idx, 0
        .set            .Lpic_gp, 0
    .macro endfunc
      .if .Lpic_idx
        .align          2
        .altmacro
        put_pic         %(.Lpic_idx - 1)
        .noaltmacro
      .endif
ELF     .size   \name, . - \name
        .endfunc
        .purgem endfunc
    .endm
        .text
        .align          2
    .if \export
        .global EXTERN_ASM\name
EXTERN_ASM\name:
    .endif
ELF     .type   \name, %function
        .func   \name
\name:
.endm

.macro  const   name, align=2
    .macro endconst
ELF     .size   \name, . - \name
        .purgem endconst
    .endm
        .section        .rodata
        .align          \align
\name:
.endm

#if !HAVE_ARMV6T2_EXTERNAL
.macro  movw    rd, val
        mov     \rd, \val &  255
        orr     \rd, \val & ~255
.endm
#endif

.macro  mov32   rd, val
#if HAVE_ARMV6T2_EXTERNAL
        movw            \rd, #(\val) & 0xffff
    .if (\val) >> 16
        movt            \rd, #(\val) >> 16
    .endif
#else
        ldr             \rd, =\val
#endif
.endm

.macro  put_pic         num
        put_pic_\num
.endm

.macro  do_def_pic      num, val, label
    .macro put_pic_\num
      .if \num
        .altmacro
        put_pic         %(\num - 1)
        .noaltmacro
      .endif
\label: .word           \val
        .purgem         put_pic_\num
    .endm
.endm

.macro  def_pic         val, label
        .altmacro
        do_def_pic      %.Lpic_idx, \val, \label
        .noaltmacro
        .set            .Lpic_idx, .Lpic_idx + 1
.endm

.macro  ldpic           rd,  val, indir=0
        ldr             \rd, .Lpicoff\@
.Lpic\@:
    .if \indir
A       ldr             \rd, [pc, \rd]
T       add             \rd, pc
T       ldr             \rd, [\rd]
    .else
        add             \rd, pc
    .endif
        def_pic         \val - (.Lpic\@ + (8 >> CONFIG_THUMB)), .Lpicoff\@
.endm

.macro  movrel rd, val
#if CONFIG_PIC
        ldpic           \rd, \val
#elif HAVE_ARMV6T2_EXTERNAL && !defined(__APPLE__)
        movw            \rd, #:lower16:\val
        movt            \rd, #:upper16:\val
#else
        ldr             \rd, =\val
#endif
.endm

.macro  movrelx         rd,  val, gp
#if CONFIG_PIC && defined(__ELF__)
    .ifnb \gp
      .if .Lpic_gp
        .unreq          gp
      .endif
        gp      .req    \gp
        ldpic           gp,  _GLOBAL_OFFSET_TABLE_
    .elseif !.Lpic_gp
        gp      .req    r12
        ldpic           gp,  _GLOBAL_OFFSET_TABLE_
    .endif
        .set            .Lpic_gp, 1
        ldr             \rd, .Lpicoff\@
        ldr             \rd, [gp, \rd]
        def_pic         \val(GOT), .Lpicoff\@
#elif CONFIG_PIC && defined(__APPLE__)
        ldpic           \rd, .Lpic\@, indir=1
        .non_lazy_symbol_pointer
.Lpic\@:
        .indirect_symbol \val
        .word           0
        .text
#else
        movrel          \rd, \val
#endif
.endm

.macro  add_sh          rd,  rn,  rm,  sh:vararg
A       add             \rd, \rn, \rm, \sh
T       mov             \rm, \rm, \sh
T       add             \rd, \rn, \rm
.endm

.macro  ldr_pre         rt,  rn,  rm:vararg
A       ldr             \rt, [\rn, \rm]!
T       add             \rn, \rn, \rm
T       ldr             \rt, [\rn]
.endm

.macro  ldr_dpre        rt,  rn,  rm:vararg
A       ldr             \rt, [\rn, -\rm]!
T       sub             \rn, \rn, \rm
T       ldr             \rt, [\rn]
.endm

.macro  ldr_nreg        rt,  rn,  rm:vararg
A       ldr             \rt, [\rn, -\rm]
T       sub             \rt, \rn, \rm
T       ldr             \rt, [\rt]
.endm

.macro  ldr_post        rt,  rn,  rm:vararg
A       ldr             \rt, [\rn], \rm
T       ldr             \rt, [\rn]
T       add             \rn, \rn, \rm
.endm

.macro  ldrd_reg        rt,  rt2, rn,  rm
A       ldrd            \rt, \rt2, [\rn, \rm]
T       add             \rt, \rn, \rm
T       ldrd            \rt, \rt2, [\rt]
.endm

.macro  ldrd_post       rt,  rt2, rn,  rm
A       ldrd            \rt, \rt2, [\rn], \rm
T       ldrd            \rt, \rt2, [\rn]
T       add             \rn, \rn, \rm
.endm

.macro  ldrh_pre        rt,  rn,  rm
A       ldrh            \rt, [\rn, \rm]!
T       add             \rn, \rn, \rm
T       ldrh            \rt, [\rn]
.endm

.macro  ldrh_dpre       rt,  rn,  rm
A       ldrh            \rt, [\rn, -\rm]!
T       sub             \rn, \rn, \rm
T       ldrh            \rt, [\rn]
.endm

.macro  ldrh_post       rt,  rn,  rm
A       ldrh            \rt, [\rn], \rm
T       ldrh            \rt, [\rn]
T       add             \rn, \rn, \rm
.endm

.macro  ldrb_post       rt,  rn,  rm
A       ldrb            \rt, [\rn], \rm
T       ldrb            \rt, [\rn]
T       add             \rn, \rn, \rm
.endm

.macro  str_post       rt,  rn,  rm:vararg
A       str             \rt, [\rn], \rm
T       str             \rt, [\rn]
T       add             \rn, \rn, \rm
.endm

.macro  strb_post       rt,  rn,  rm:vararg
A       strb            \rt, [\rn], \rm
T       strb            \rt, [\rn]
T       add             \rn, \rn, \rm
.endm

.macro  strd_post       rt,  rt2, rn,  rm
A       strd            \rt, \rt2, [\rn], \rm
T       strd            \rt, \rt2, [\rn]
T       add             \rn, \rn, \rm
.endm

.macro  strh_pre        rt,  rn,  rm
A       strh            \rt, [\rn, \rm]!
T       add             \rn, \rn, \rm
T       strh            \rt, [\rn]
.endm

.macro  strh_dpre       rt,  rn,  rm
A       strh            \rt, [\rn, -\rm]!
T       sub             \rn, \rn, \rm
T       strh            \rt, [\rn]
.endm

.macro  strh_post       rt,  rn,  rm
A       strh            \rt, [\rn], \rm
T       strh            \rt, [\rn]
T       add             \rn, \rn, \rm
.endm

.macro  strh_dpost       rt,  rn,  rm
A       strh            \rt, [\rn], -\rm
T       strh            \rt, [\rn]
T       sub             \rn, \rn, \rm
.endm

#if HAVE_VFP_ARGS
        .eabi_attribute 28, 1
#   define VFP
#   define NOVFP @
#else
#   define VFP   @
#   define NOVFP
#endif

#define GLUE(a, b) a ## b
#define JOIN(a, b) GLUE(a, b)
#define X(s) JOIN(EXTERN_ASM, s)

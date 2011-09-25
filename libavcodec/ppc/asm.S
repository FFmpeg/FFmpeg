/*
 * Copyright (c) 2009 Loren Merritt
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"

#define GLUE(a, b) a ## b
#define JOIN(a, b) GLUE(a, b)
#define X(s) JOIN(EXTERN_ASM, s)

#if ARCH_PPC64

#define PTR  .quad
#define lp   ld
#define lpx  ldx
#define stp  std
#define stpu stdu
#define PS   8
#define L(s) JOIN(., s)

.macro extfunc name
    .global X(\name)
    .section .opd, "aw"
X(\name):
    .quad L(\name), .TOC.@tocbase, 0
    .previous
    .type X(\name), STT_FUNC
L(\name):
.endm

.macro movrel rd, sym, gp
    ld      \rd, \sym@got(r2)
.endm

.macro get_got rd
.endm

#else /* ARCH_PPC64 */

#define PTR  .int
#define lp   lwz
#define lpx  lwzx
#define stp  stw
#define stpu stwu
#define PS   4
#define L(s) s

.macro extfunc name
    .global X(\name)
    .type   X(\name), STT_FUNC
X(\name):
\name:
.endm

.macro movrel rd, sym, gp
#if CONFIG_PIC
    lwz     \rd, \sym@got(\gp)
#else
    lis     \rd, \sym@ha
    la      \rd, \sym@l(\rd)
#endif
.endm

.macro get_got rd
#if CONFIG_PIC
    bcl     20, 31, .Lgot\@
.Lgot\@:
    mflr    \rd
    addis   \rd, \rd, _GLOBAL_OFFSET_TABLE_ - .Lgot\@@ha
    addi    \rd, \rd, _GLOBAL_OFFSET_TABLE_ - .Lgot\@@l
#endif
.endm

#endif /* ARCH_PPC64 */

#if HAVE_IBM_ASM

.macro DEFINE_REG n
    .equiv r\n, \n
    .equiv f\n, \n
    .equiv v\n, \n
.endm

DEFINE_REG 0
DEFINE_REG 1
DEFINE_REG 2
DEFINE_REG 3
DEFINE_REG 4
DEFINE_REG 5
DEFINE_REG 6
DEFINE_REG 7
DEFINE_REG 8
DEFINE_REG 9
DEFINE_REG 10
DEFINE_REG 11
DEFINE_REG 12
DEFINE_REG 13
DEFINE_REG 14
DEFINE_REG 15
DEFINE_REG 16
DEFINE_REG 17
DEFINE_REG 18
DEFINE_REG 19
DEFINE_REG 20
DEFINE_REG 21
DEFINE_REG 22
DEFINE_REG 23
DEFINE_REG 24
DEFINE_REG 25
DEFINE_REG 26
DEFINE_REG 27
DEFINE_REG 28
DEFINE_REG 29
DEFINE_REG 30
DEFINE_REG 31

#endif /* HAVE_IBM_ASM */

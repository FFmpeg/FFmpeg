/*
 * Copyright © 2022 Rémi Denis-Courmont.
 * Loosely based on earlier work copyrighted by Måns Rullgård, 2008.
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

#if defined (__riscv_float_abi_soft)
#define NOHWF
#define NOHWD
#define HWF   #
#define HWD   #
#elif defined (__riscv_float_abi_single)
#define NOHWF #
#define NOHWD
#define HWF
#define HWD   #
#else
#define NOHWF #
#define NOHWD #
#define HWF
#define HWD
#endif

        .macro func sym, ext=
            .text
            .align 2

            .option push
            .ifnb \ext
            .option arch, +\ext
            .endif

            .global \sym
            .hidden \sym
            .type   \sym, %function
            \sym:

            .macro endfunc
                .size   \sym, . - \sym
                .option pop
                .previous
                .purgem endfunc
            .endm
        .endm

        .macro const sym, align=3, relocate=0
            .if \relocate
                .pushsection .data.rel.ro
            .else
                .pushsection .rodata
            .endif
            .align \align
            \sym:

            .macro endconst
                .size  \sym, . - \sym
                .popsection
                .purgem endconst
            .endm
        .endm

#if !defined (__riscv_zba)
        /* SH{1,2,3}ADD definitions for pre-Zba assemblers */
        .macro  shnadd n, rd, rs1, rs2
        .insn r OP, 2 * \n, 16, \rd, \rs1, \rs2
        .endm

        .macro  sh1add rd, rs1, rs2
        shnadd  1, \rd, \rs1, \rs2
        .endm

        .macro  sh2add rd, rs1, rs2
        shnadd  2, \rd, \rs1, \rs2
        .endm

        .macro  sh3add rd, rs1, rs2
        shnadd  3, \rd, \rs1, \rs2
        .endm
#endif

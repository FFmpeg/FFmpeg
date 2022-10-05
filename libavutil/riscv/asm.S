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

        /* Convenience macro to load a Vector type (vtype) as immediate */
        .macro  lvtypei rd, e, m=m1, tp=tu, mp=mu

        .ifc \e,e8
        .equ ei, 0
        .else
        .ifc \e,e16
        .equ ei, 8
        .else
        .ifc \e,e32
        .equ ei, 16
        .else
        .ifc \e,e64
        .equ ei, 24
        .else
        .error "Unknown element type"
        .endif
        .endif
        .endif
        .endif

        .ifc \m,m1
        .equ mi, 0
        .else
        .ifc \m,m2
        .equ mi, 1
        .else
        .ifc \m,m4
        .equ mi, 2
        .else
        .ifc \m,m8
        .equ mi, 3
        .else
        .ifc \m,mf8
        .equ mi, 5
        .else
        .ifc \m,mf4
        .equ mi, 6
        .else
        .ifc \m,mf2
        .equ mi, 7
        .else
        .error "Unknown multiplier"
        .equ mi, 3
        .endif
        .endif
        .endif
        .endif
        .endif
        .endif
        .endif

        .ifc \tp,tu
        .equ tpi, 0
        .else
        .ifc \tp,ta
        .equ tpi, 64
        .else
        .error "Unknown tail policy"
        .endif
        .endif

        .ifc \mp,mu
        .equ mpi, 0
        .else
        .ifc \mp,ma
        .equ mpi, 128
        .else
        .error "Unknown mask policy"
        .endif
        .endif

        li      \rd, (ei | mi | tpi | mpi)
        .endm

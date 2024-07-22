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

        .macro  archadd ext=, more:vararg
            .ifnb   \ext
                .ifc    \ext, b
                # B was defined later, is known to fewer assemblers.
                archadd zba, zbb, zbs
                .else
                    .option arch, +\ext
                .endif
                archadd \more
            .endif
        .endm

        .macro func sym, exts:vararg
            .text
            .option push
            archadd \exts

            .global \sym
            .hidden \sym
            .type   \sym, %function
            .option push
            .option norvc
            .align  2
            \sym:
            .option pop

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

#if !defined (__riscv_zicfilp)
        .macro  lpad    lpl
        auipc   zero, \lpl
        .endm
#endif

#if defined (__riscv_v_elen)
# define RV_V_ELEN __riscv_v_elen
#else
/* Run-time detection of the V extension implies ELEN >= 64. */
# define RV_V_ELEN 64
#endif
#if RV_V_ELEN == 32
# define VSEW_MAX 2
#else
# define VSEW_MAX 3
#endif

        .macro  parse_vtype ew, tp, mp
        .ifc    \ew,e8
        .equ    vsew, 0
        .else
        .ifc    \ew,e16
        .equ    vsew, 1
        .else
        .ifc    \ew,e32
        .equ    vsew, 2
        .else
        .ifc    \ew,e64
        .equ    vsew, 3
        .else
        .error  "Unknown element width \ew"
        .endif
        .endif
        .endif
        .endif

        .ifc    \tp,tu
        .equ    tp, 0
        .else
        .ifc    \tp,ta
        .equ    tp, 1
        .else
        .error  "Unknown tail policy \tp"
        .endif
        .endif

        .ifc    \mp,mu
        .equ    mp, 0
        .else
        .ifc    \mp,ma
        .equ    mp, 1
        .else
        .error  "Unknown mask policy \mp"
        .endif
        .endif
        .endm

        /**
         * Gets the vector type with the smallest suitable LMUL value.
         * @param[out] rd vector type destination register
         * @param vl vector length constant
         * @param ew element width: e8, e16, e32 or e64
         * @param tp tail policy: tu or ta
         * @param mp mask policty: mu or ma
         */
        .macro  vtype_ivli rd, avl, ew, tp=tu, mp=mu
        .if     \avl <= 1
        .equ    log2vl, 0
        .elseif \avl <= 2
        .equ    log2vl, 1
        .elseif \avl <= 4
        .equ    log2vl, 2
        .elseif \avl <= 8
        .equ    log2vl, 3
        .elseif \avl <= 16
        .equ    log2vl, 4
        .elseif \avl <= 32
        .equ    log2vl, 5
        .elseif \avl <= 64
        .equ    log2vl, 6
        .elseif \avl <= 128
        .equ    log2vl, 7
        .else
        .error  "Vector length \avl out of range"
        .endif
        parse_vtype \ew, \tp, \mp
        csrr    \rd, vlenb
        clz     \rd, \rd
        addi    \rd, \rd, log2vl + 1 + VSEW_MAX - __riscv_xlen
        max     \rd, \rd, zero // VLMUL must be >= VSEW - VSEW_MAX
        .if     vsew < VSEW_MAX
        addi    \rd, \rd, vsew - VSEW_MAX
        andi    \rd, \rd, 7
        .endif
        ori     \rd, \rd, (vsew << 3) | (tp << 6) | (mp << 7)
        .endm

        /**
         * Gets the vector type with the smallest suitable LMUL value.
         * @param[out] rd vector type destination register
         * @param rs vector length source register
         * @param[out] tmp temporary register to be clobbered
         * @param ew element width: e8, e16, e32 or e64
         * @param tp tail policy: tu or ta
         * @param mp mask policty: mu or ma
         * @param addend optional addend for the vector length register
         */
        .macro  vtype_vli rd, rs, tmp, ew, tp=tu, mp=mu, addend=0
        parse_vtype \ew, \tp, \mp
        /*
         * The difference between the CLZ's notionally equals the VLMUL value
         * for 4-bit elements. But we want the value for SEW_MAX-bit elements.
         */
        slli    \tmp, \rs, 1 + VSEW_MAX
        .if \addend - 1
        addi    \tmp, \tmp, \addend - 1
        .endif
        csrr    \rd, vlenb
        clz     \tmp, \tmp
        clz     \rd, \rd
        sub     \rd, \rd, \tmp
        max     \rd, \rd, zero // VLMUL must be >= VSEW - VSEW_MAX
        .if     vsew < VSEW_MAX
        addi    \rd, \rd, vsew - VSEW_MAX
        andi    \rd, \rd, 7
        .endif
        ori     \rd, \rd, (vsew << 3) | (tp << 6) | (mp << 7)
        .endm

        /**
         * Widens a vector type.
         * @param[out] rd widened vector type destination register
         * @param rs vector type source register
         * @param n number of times to widen (once by default)
         */
        .macro  vwtypei rd, rs, n=1
        xori    \rd, \rs, 4
        addi    \rd, \rd, (\n) * 011
        xori    \rd, \rd, 4
        .endm

        /**
         * Narrows a vector type.
         * @param[out] rd narrowed vector type destination register
         * @param rs vector type source register
         * @param n number of times to narrow (once by default)
         */
        .macro  vntypei rd, rs, n=1
        vwtypei \rd, \rs, -(\n)
        .endm

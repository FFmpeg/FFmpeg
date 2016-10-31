/****************************************************************************
 * Assembly testing and benchmarking tool
 * Copyright (c) 2015 Martin Storsjo
 * Copyright (c) 2015 Janne Grunau
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *****************************************************************************/

#include "libavutil/arm/asm.S"

const register_init
    .quad 0x21f86d66c8ca00ce
    .quad 0x75b6ba21077c48ad
    .quad 0xed56bb2dcb3c7736
    .quad 0x8bda43d3fd1a7e06
    .quad 0xb64a9c9e5d318408
    .quad 0xdf9a54b303f1d3a3
    .quad 0x4a75479abd64e097
    .quad 0x249214109d5d1c88
endconst

const error_message
    .asciz "failed to preserve register"
endconst

@ max number of args used by any asm function.
#define MAX_ARGS 15

#define ARG_STACK 4*(MAX_ARGS - 2)

.macro clobbercheck variant
.equ pushed, 4*9
function checkasm_checked_call_\variant, export=1
    push        {r4-r11, lr}
.ifc \variant, vfp
    vpush       {d8-d15}
    fmrx        r4,  FPSCR
    push        {r4}
.equ pushed, pushed + 16*4 + 4
.endif

    movrel      r12, register_init
.ifc \variant, vfp
    vldm        r12, {d8-d15}
.endif
    ldm         r12, {r4-r11}

    sub         sp,  sp,  #ARG_STACK
.equ pos, 0
.rept MAX_ARGS-2
    ldr         r12, [sp, #ARG_STACK + pushed + 8 + pos]
    str         r12, [sp, #pos]
.equ pos, pos + 4
.endr

    mov         r12, r0
    mov         r0,  r2
    mov         r1,  r3
    ldrd        r2,  r3,  [sp, #ARG_STACK + pushed]
    blx         r12
    add         sp,  sp,  #ARG_STACK

    push        {r0, r1}
    movrel      r12, register_init
    mov         r3,  #0
.ifc \variant, vfp
.macro check_reg_vfp, dreg, inc=8
    ldrd        r0,  r1,  [r12], #\inc
    vmov        r2,  lr,  \dreg
    eor         r0,  r0,  r2
    eor         r1,  r1,  lr
    orr         r3,  r3,  r0
    orr         r3,  r3,  r1
.endm

.irp n, 8, 9, 10, 11, 12, 13, 14
    check_reg_vfp d\n
.endr
    check_reg_vfp d15, -56
.purgem check_reg_vfp

    fmrx        r0,  FPSCR
    ldr         r1,  [sp, #8]
    eor         r0,  r0,  r1
    @ Ignore changes in the topmost 5 bits
    lsl         r0,  r0,  #5
    orr         r3,  r3,  r0
.endif

.macro check_reg reg1, reg2=
    ldrd        r0,  r1,  [r12], #8
    eor         r0,  r0,  \reg1
    orrs        r3,  r3,  r0
.ifnb \reg2
    eor         r1,  r1,  \reg2
    orrs        r3,  r3,  r1
.endif
.endm
    check_reg   r4,  r5
    check_reg   r6,  r7
@ r9 is a volatile register in the ios ABI
#ifdef __APPLE__
    check_reg   r8
#else
    check_reg   r8,  r9
#endif
    check_reg   r10, r11
.purgem check_reg

    beq         0f

    movrel      r0, error_message
    blx         X(checkasm_fail_func)
0:
    pop         {r0, r1}
.ifc \variant, vfp
    pop         {r2}
    fmxr        FPSCR, r2
    vpop        {d8-d15}
.endif
    pop         {r4-r11, pc}
endfunc
.endm

#if HAVE_VFP || HAVE_NEON
clobbercheck vfp
#endif
clobbercheck novfp

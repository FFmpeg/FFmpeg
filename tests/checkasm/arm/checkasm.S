/****************************************************************************
 * Assembly testing and benchmarking tool
 * Copyright (c) 2015 Martin Storsjo
 * Copyright (c) 2015 Janne Grunau
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *****************************************************************************/

#include "libavutil/arm/asm.S"

/* override fpu so that NEON instructions are rejected */
#if HAVE_VFP
FPU     .fpu            vfp
ELF     .eabi_attribute 10, 0           @ suppress Tag_FP_arch
#endif

const register_init, align=3
    .quad 0x21f86d66c8ca00ce
    .quad 0x75b6ba21077c48ad
    .quad 0xed56bb2dcb3c7736
    .quad 0x8bda43d3fd1a7e06
    .quad 0xb64a9c9e5d318408
    .quad 0xdf9a54b303f1d3a3
    .quad 0x4a75479abd64e097
    .quad 0x249214109d5d1c88
endconst

const error_message_fpscr
    .asciz "failed to preserve register FPSCR, changed bits: %x"
error_message_gpr:
    .asciz "failed to preserve register r%d"
error_message_vfp:
    .asciz "failed to preserve register d%d"
endconst

@ max number of args used by any asm function.
#define MAX_ARGS 15

#define ARG_STACK 4*(MAX_ARGS - 2)

@ align the used stack space to 8 to preserve the stack alignment
#define ARG_STACK_A (((ARG_STACK + pushed + 7) & ~7) - pushed)

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

    sub         sp,  sp,  #ARG_STACK_A
.equ pos, 0
.rept MAX_ARGS-2
    ldr         r12, [sp, #ARG_STACK_A + pushed + 8 + pos]
    str         r12, [sp, #pos]
.equ pos, pos + 4
.endr

    mov         r12, r0
    mov         r0,  r2
    mov         r1,  r3
    ldrd        r2,  r3,  [sp, #ARG_STACK_A + pushed]
    blx         r12
    add         sp,  sp,  #ARG_STACK_A

    push        {r0, r1}
    movrel      r12, register_init
.ifc \variant, vfp
.macro check_reg_vfp, dreg, offset
    ldrd        r2,  r3,  [r12, #8 * (\offset)]
    vmov        r0,  lr,  \dreg
    eor         r2,  r2,  r0
    eor         r3,  r3,  lr
    orrs        r2,  r2,  r3
    bne         4f
.endm

.irp n, 8, 9, 10, 11, 12, 13, 14, 15
    @ keep track of the checked double/SIMD register
    mov         r1,  #\n
    check_reg_vfp d\n, \n-8
.endr
.purgem check_reg_vfp

    fmrx        r1,  FPSCR
    ldr         r3,  [sp, #8]
    eor         r1,  r1,  r3
    @ Ignore changes in bits 0-4 and 7
    bic         r1,  r1,  #0x9f
    @ Ignore changes in the topmost 5 bits
    bics        r1,  r1,  #0xf8000000
    bne         3f
.endif

    @ keep track of the checked GPR
    mov         r1,  #4
.macro check_reg reg1, reg2=
    ldrd        r2,  r3,  [r12], #8
    eors        r2,  r2,  \reg1
    bne         2f
    add         r1,  r1,  #1
.ifnb \reg2
    eors        r3,  r3,  \reg2
    bne         2f
.endif
    add         r1,  r1,  #1
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

    b           0f
4:
    movrel      r0, error_message_vfp
    b           1f
3:
    movrel      r0, error_message_fpscr
    b           1f
2:
    movrel      r0, error_message_gpr
1:
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

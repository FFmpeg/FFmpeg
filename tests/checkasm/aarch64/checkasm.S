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

#include "libavutil/aarch64/asm.S"

const register_init
    .quad 0x21f86d66c8ca00ce
    .quad 0x75b6ba21077c48ad
    .quad 0xed56bb2dcb3c7736
    .quad 0x8bda43d3fd1a7e06
    .quad 0xb64a9c9e5d318408
    .quad 0xdf9a54b303f1d3a3
    .quad 0x4a75479abd64e097
    .quad 0x249214109d5d1c88
    .quad 0x1a1b2550a612b48c
    .quad 0x79445c159ce79064
    .quad 0x2eed899d5a28ddcd
    .quad 0x86b2536fcd8cf636
    .quad 0xb0856806085e7943
    .quad 0x3f2bf84fc0fcca4e
    .quad 0xacbd382dcf5b8de2
    .quad 0xd229e1f5b281303f
    .quad 0x71aeaff20b095fd9
    .quad 0xab63e2e11fa38ed9
endconst


const error_message
    .asciz "failed to preserve register"
endconst


// max number of args used by any asm function.
#define MAX_ARGS 15

#define ARG_STACK ((8*(MAX_ARGS - 7) + 15) & ~15)

function checkasm_checked_call, export=1
    stp         x29, x30, [sp, #-16]!
    mov         x29, sp
    stp         x19, x20, [sp, #-16]!
    stp         x21, x22, [sp, #-16]!
    stp         x23, x24, [sp, #-16]!
    stp         x25, x26, [sp, #-16]!
    stp         x27, x28, [sp, #-16]!
    stp         d8,  d9,  [sp, #-16]!
    stp         d10, d11, [sp, #-16]!
    stp         d12, d13, [sp, #-16]!
    stp         d14, d15, [sp, #-16]!

    movrel      x9, register_init
    ldp         d8,  d9,  [x9], #16
    ldp         d10, d11, [x9], #16
    ldp         d12, d13, [x9], #16
    ldp         d14, d15, [x9], #16
    ldp         x19, x20, [x9], #16
    ldp         x21, x22, [x9], #16
    ldp         x23, x24, [x9], #16
    ldp         x25, x26, [x9], #16
    ldp         x27, x28, [x9], #16

    sub         sp,  sp,  #ARG_STACK
.equ pos, 0
// the first stacked arg is copied to x7
.rept MAX_ARGS-7
    ldr         x9, [x29, #16 + 8 + pos]
    str         x9, [sp, #pos]
.equ pos, pos + 8
.endr

    mov         x12, x0
    mov         x0,  x1
    mov         x1,  x2
    mov         x2,  x3
    mov         x3,  x4
    mov         x4,  x5
    mov         x5,  x6
    mov         x6,  x7
    ldr         x7,  [x29, #16]
    blr         x12
    add         sp,  sp,  #ARG_STACK
    stp         x0,  x1,  [sp, #-16]!
    movrel      x9, register_init
    movi        v3.8h,  #0

.macro check_reg_neon reg1, reg2
    ldr         q0,  [x9], #16
    uzp1        v1.2d,  v\reg1\().2d, v\reg2\().2d
    eor         v0.16b, v0.16b, v1.16b
    orr         v3.16b, v3.16b, v0.16b
.endm
    check_reg_neon  8,  9
    check_reg_neon  10, 11
    check_reg_neon  12, 13
    check_reg_neon  14, 15
    uqxtn       v3.8b,  v3.8h
    umov        x3,  v3.d[0]

.macro check_reg reg1, reg2
    ldp         x0,  x1,  [x9], #16
    eor         x0,  x0,  \reg1
    eor         x1,  x1,  \reg2
    orr         x3,  x3,  x0
    orr         x3,  x3,  x1
.endm
    check_reg   x19, x20
    check_reg   x21, x22
    check_reg   x23, x24
    check_reg   x25, x26
    check_reg   x27, x28

    cbz         x3,  0f

    movrel      x0, error_message
    bl          X(checkasm_fail_func)
0:
    ldp         x0,  x1,  [sp], #16
    ldp         d14, d15, [sp], #16
    ldp         d12, d13, [sp], #16
    ldp         d10, d11, [sp], #16
    ldp         d8,  d9,  [sp], #16
    ldp         x27, x28, [sp], #16
    ldp         x25, x26, [sp], #16
    ldp         x23, x24, [sp], #16
    ldp         x21, x22, [sp], #16
    ldp         x19, x20, [sp], #16
    ldp         x29, x30, [sp], #16
    ret
endfunc
